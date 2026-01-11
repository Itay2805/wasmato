#include "vmar.h"

#include "arch/paging.h"
#include "internal/virt.h"
#include "kernel/alloc.h"
#include "lib/assert.h"
#include "lib/string.h"

/**
 * Lock to protect all VMAR modifications, in theory we can eventually
 * perform a per VMAR lock, but this is a bit complicated so for now
 * we will just have a single global lock
 */
static irq_spinlock_t m_vmar_lock = IRQ_SPINLOCK_INIT;

static bool vmar_region_less(struct rb_node* a, const struct rb_node* b) {
    const mapping_t* va = containerof(a, mapping_t, node);
    const mapping_t* vb = containerof(b, mapping_t, node);
    return va->start < vb->start;
}

static void* vmar_find_empty_region(vmar_t* vmar, size_t page_count, size_t alignment) {
    size_t prev_region_start_page = vmar->region.page_count;

    for (struct rb_node* node = rb_last(&vmar->root); node != NULL; node = rb_prev(node)) {
        mapping_t* region = containerof(node, mapping_t, node);
        size_t region_start_page = (region->start - vmar->region.start) / PAGE_SIZE;
        size_t region_end_page = region_start_page + region->page_offset;
        size_t gap_page_count = prev_region_start_page - region_end_page;

        // does it have enough pages?
        if (gap_page_count >= page_count) {
            // can we align it while staying within the region?
            void* start = ALIGN_DOWN(vmar->region.start + PAGES_TO_SIZE(prev_region_start_page - page_count), alignment);
            void* region_end = region->start + (PAGES_TO_SIZE(region->page_count) - 1);
            if (start > region_end) {
                // we can! return the address
                return start;
            }
        }

        // update the previous region
        prev_region_start_page = region_start_page;
    }

    // does it have enough pages?
    if (page_count <= prev_region_start_page) {
        // can we align it while staying within the region?
        void* start = ALIGN_DOWN(vmar->region.start + PAGES_TO_SIZE(prev_region_start_page - page_count), alignment);
        if (start >= vmar->region.start) {
            // we can! return the address
            return start;
        }
    }

    // failed to find a gap
    return NULL;
}

typedef struct vmar_search_overlap {
    void* start;
    void* end;
} vmar_search_overlap_t;

static int vmar_cmp_overlap(const void* key, const struct rb_node* node) {
    const vmar_search_overlap_t* ctx = key;
    const mapping_t* region = containerof(node, mapping_t, node);

    // if overlaps then the key matches
    void* region_end = region->start + PAGES_TO_SIZE(region->page_count) - 1;
    if (ctx->end >= region->start && region_end >= ctx->start) {
        return 0;
    }

    // otherwise let it search
    if (ctx->start < region->start) {
        return -1;
    } else {
        return 1;
    }
}

static err_t vmar_allocate_region(
    vmar_t* vmar,
    mapping_t* region,
    bool specific,
    size_t offset, size_t size, size_t order
) {
    err_t err = NO_ERROR;

    // ensure the size is page aligned
    CHECK((size % PAGE_SIZE) == 0);
    CHECK(size > 0);
    size_t page_count = size / PAGE_SIZE;

    // the alignment that we have
    size_t alignment = 1ul << (order + 12);

    // search for an empty region where we can create the region
    void* start = NULL;
    if (specific) {
        CHECK((offset % PAGE_SIZE) == 0);

        // the start we want, ensure the alignment is correct
        start = vmar->region.start + offset;
        CHECK(((uintptr_t)start % alignment) == 0);

        // we must be above the start
        CHECK(start >= vmar->region.start);

        // ensure it doesn't overlap with anything else
        vmar_search_overlap_t key = {
            .start = start,
            .end = start + (size - 1)
        };
        CHECK(rb_find(&key, &vmar->root, vmar_cmp_overlap) == NULL);

    } else {
        // carve space from the parent, when this returns with true we are already inside of the
        // address space and should be worried about being unmapped
        start = vmar_find_empty_region(vmar, page_count, alignment);
        CHECK_ERROR(start != NULL, ERROR_OUT_OF_MEMORY);
    }

    // setup the region itself, we are going to take a ref to the object
    // so the parent vmar will keep it alive
    region->start = start;
    region->page_count = page_count;

cleanup:
    return err;
}

err_t vmar_allocate_static(
    vmar_t* parent_vmar,
    vmar_t* child_vmar,
    vmar_options_t options,
    size_t offset, size_t size, size_t order
) {
    err_t err = NO_ERROR;

    bool irq_state = irq_spinlock_acquire(&m_vmar_lock);

    // allocate a region from the vmar
    RETHROW(vmar_allocate_region(
        parent_vmar,
        &child_vmar->region,
        options & VMAR_SPECIFIC,
        offset, size, order
    ));

    // add the region
    rb_add(&child_vmar->region.node, &parent_vmar->root, vmar_region_less);

    // take a ref to the child vmar and place it in the region
    child_vmar->region.object = object_get(&child_vmar->object);

cleanup:
    irq_spinlock_release(&m_vmar_lock, irq_state);

    return err;
}

err_t vmar_allocate(
    vmar_t* parent_vmar,
    vmar_options_t options,
    size_t offset, size_t size, size_t order,
    vmar_t** child_vmar
) {
    err_t err = NO_ERROR;

    // allocate the vmar object
    vmar_t* vmar = alloc_type(vmar_t);
    CHECK_ERROR(vmar != NULL, ERROR_OUT_OF_MEMORY);
    vmar->object.type = OBJECT_TYPE_VMAR;
    vmar->object.ref_count = 1;

    // call the other function
    RETHROW(vmar_allocate_static(parent_vmar, vmar, options, offset, size, order));

    // output the new child
    *child_vmar = vmar;

cleanup:
    if (IS_ERROR(err)) {
        free_type(vmar_t, vmar);
    }

    return err;
}

mapping_t* vmar_find_mapping(vmar_t* vmar, void* ptr) {
    bool irq_state = irq_spinlock_acquire(&m_vmar_lock);
    for (;;) {
        // ensure its even in the range
        if (!vmar_contains_ptr(vmar, ptr)) {
            irq_spinlock_release(&m_vmar_lock, irq_state);
            return NULL;
        }

        // search for the children, assume the access
        // size is of at least 1 byte
        vmar_search_overlap_t key = {
            .start = ptr,
            .end = ptr + 1
        };
        struct rb_node* node = rb_find(&key, &vmar->root, vmar_cmp_overlap);
        if (node == NULL) {
            irq_spinlock_release(&m_vmar_lock, irq_state);
            return NULL;
        }

        // get the region, we are going to lock it before we actually use it
        mapping_t* region = containerof(node, mapping_t, node);

        // if we got a VMAR then we need to search it for a child
        if (region->object->type == OBJECT_TYPE_VMAR) {
            vmar = containerof(region->object, vmar_t, object);

        } else if (region->object->type == OBJECT_TYPE_VMO) {
            // otherwise we found the region
            irq_spinlock_release(&m_vmar_lock, irq_state);
            return region;

        } else {
            ASSERT(!"Invalid object type");
        }
    }
}

err_t vmar_map(
    vmar_t* vmar,
    vmar_map_options_t options,
    size_t vmar_offset,
    vmo_t* vmo,
    size_t vmo_offset, size_t len, size_t order,
    void** mapped_addr
) {
    err_t err = NO_ERROR;
    mapping_t* mapping = NULL;

    bool irq_state = irq_spinlock_acquire(&m_vmar_lock);

    // ensure alignment
    CHECK((vmo_offset % PAGE_SIZE) == 0);
    CHECK((len % PAGE_SIZE) == 0);
    if (options & VMAR_MAP_SPECIFIC) {
        CHECK((vmar_offset % PAGE_SIZE) == 0);
    } else {
        CHECK(vmar_offset == 0);
    }

    // ensure the range is within the vmo
    size_t vmo_size = vmo_get_size(vmo);
    uint64_t vmo_top_address = 0;
    CHECK(!__builtin_add_overflow(vmo_offset, len, &vmo_top_address));

    // allocate the mapping object, use the inline mapping
    // whenever it is free
    mapping = NULL;
    if (vmo->inline_mapping.object == NULL) {
        mapping = &vmo->inline_mapping;
    } else {
        mapping = alloc_type(mapping_t);
        CHECK_ERROR(mapping != NULL, ERROR_OUT_OF_MEMORY);
        mapping->dynamic = true;
    }
    mapping->page_offset = vmo_offset / PAGE_SIZE;
    mapping->exec = (options & VMAR_MAP_EXECUTE);
    mapping->write = (options & VMAR_MAP_WRITE);
    mapping->object = &vmo->object;

    // allocate a region to be used
    RETHROW(vmar_allocate_region(vmar, mapping, options & VMAR_MAP_SPECIFIC, vmar_offset, len, order));

    // if we want to page it in right now then do that
    if (options & VMAR_MAP_POPULATE) {
        RETHROW(virt_map_and_populate_vmo(mapping));
    }

    // finally we can commit the region
    rb_add(&mapping->node, &vmar->root, vmar_region_less);

    // output the mapped address
    if (mapped_addr != NULL) {
        *mapped_addr = mapping->start;
    }

cleanup:
    if (IS_ERROR(err)) {
        if (mapping != NULL && mapping->dynamic) {
            free_type(mapping_t, mapping);
        }
    }

    irq_spinlock_release(&m_vmar_lock, irq_state);

    return err;
}

/**
 * Unmaps all VMO mappings and destroys all sub-regions within the
 * given range
 *
 * @param vmar  [IN] The VMAR to unmap from
 * @param addr  [IN] The address to unmap from
 * @param len   [IN] The length to unmap
 */
void vmar_unmap(vmar_t* vmar, void* addr, size_t len) {
    ASSERT(!"TODO: unmap");
}


void vmar_destroy(vmar_t* vmar) {
    // must already be unmapped at this point, given that the region holds
    // a ref on itself while being mapped
    ASSERT(vmar->region.start == NULL);
    free_type(vmar_t, vmar);
}

static void vmar_print_tree_rec(mapping_t* region, char* prefix, size_t plen, bool is_last) {
    if (plen) {
        debug_print("%s", prefix);
        debug_print("%s", is_last ? "└── " : "├── ");
    }

    const char* name = region->object->name;
    if (name == NULL) {
        name = "<anonymous>";
    }

    const char* type_str = NULL;
    if (region->object->type == OBJECT_TYPE_VMO) {
        type_str = "VMO ";
    } else if (region->object->type == OBJECT_TYPE_VMAR) {
        type_str = "VMAR";
    } else {
        ASSERT(false);
    }

    uintptr_t start = (uintptr_t)region->start;
    uintptr_t end = (uintptr_t)(region->start + (PAGES_TO_SIZE(region->page_count) - 1));
    debug_print("%s: 0x%08x'%08x-0x%08x'%08x: %ld pages [%s]\n",
        type_str,
        (uint32_t)(start >> 32), (uint32_t)start,
        (uint32_t)(end >> 32), (uint32_t)end,
        region->page_count,
        name
    );

    if (region->object->type == OBJECT_TYPE_VMAR) {
        vmar_t* vmar = containerof(region->object, vmar_t, object);

        // this is a vmar that might have children
        if (rb_first(&vmar->root) == NULL) {
            // no children, exit
            return;
        }

        const char *ext = is_last ? "    " : "│   ";
        memcpy(prefix + plen, ext, 4);
        size_t new_plen = plen + 4;
        prefix[new_plen] = '\0';

        for (struct rb_node* n = rb_first(&vmar->root); n != NULL; n = rb_next(n)) {
            mapping_t* mapping = containerof(n, mapping_t, node);
            vmar_print_tree_rec(mapping, prefix, new_plen, rb_next(n) == NULL);
        }

        prefix[plen] = '\0';
    }
}

void vmar_print(vmar_t* vmar) {
    bool irq_state = irq_spinlock_acquire(&m_vmar_lock);
    char prefix[256] = {0};
    vmar_print_tree_rec(&vmar->region, prefix, 0, true);
    irq_spinlock_release(&m_vmar_lock, irq_state);
}
