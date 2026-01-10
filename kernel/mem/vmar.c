#include "vmar.h"

#include "arch/paging.h"
#include "internal/virt.h"
#include "kernel/alloc.h"
#include "lib/assert.h"
#include "lib/string.h"

static bool vmar_region_less(struct rb_node* a, const struct rb_node* b) {
    const vmar_region_t* va = containerof(a, vmar_region_t, node);
    const vmar_region_t* vb = containerof(b, vmar_region_t, node);
    return va->start < vb->start;
}

static void* vmar_find_empty_region(vmar_t* vmar, size_t page_count, size_t alignment) {
    size_t prev_region_start_page = vmar->region.page_count;

    for (struct rb_node* node = rb_last(&vmar->root); node != NULL; node = rb_prev(node)) {
        vmar_region_t* region = containerof(node, vmar_region_t, node);
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

    // we could not find anything between the gaps, attempt to check if
    // the gap between the bump alloc and the first region is fine
    size_t bump_top_page = (vmar->bump_alloc_top - vmar->region.start) / PAGE_SIZE;

    // does it have enough pages?
    if (bump_top_page >= page_count) {
        // can we align it while staying within the region?
        void* start = ALIGN_DOWN(vmar->region.start + PAGES_TO_SIZE(prev_region_start_page - page_count), alignment);
        if (start >= vmar->bump_alloc_top) {
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
    const vmar_region_t* region = containerof(node, vmar_region_t, node);

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

static void vmar_commit_region(vmar_t* vmar, vmar_region_t* region) {
    // set the region fields and add the to rbtree
    rb_add(&region->node, &vmar->root, vmar_region_less);

    // update the top address of the bump allocator
    if (region->start < vmar->bump_alloc_max) {
        vmar->bump_alloc_max = region->start;
    }
}

static err_t vmar_allocate_region(
    vmar_t* vmar,
    vmar_region_t* region,
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

        // we must be above the top of the bump
        CHECK(start >= vmar->bump_alloc_top);

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

    // TODO: replace with per vmar lock
    virt_lock();

    // allocate a region from the vmar
    RETHROW(vmar_allocate_region(
        parent_vmar,
        &child_vmar->region,
        options & VMAR_SPECIFIC,
        offset, size, order
    ));

    // we can commit the region right away
    vmar_commit_region(parent_vmar, &child_vmar->region);

    // set the bump to the start, so it can always be used as the
    // offset to the start of the region that is alloctable by
    // vmars/vmos
    child_vmar->bump_alloc_top = child_vmar->region.start;

    // if we want to allow a bump allocator set the region end
    if (options & VMAR_CAN_BUMP) {
        child_vmar->bump_alloc_max = child_vmar->region.start + size;
    } else {
        child_vmar->bump_alloc_max = child_vmar->bump_alloc_top;
    }

    // take a ref to the child vmar and place it in the region
    child_vmar->region.object = object_get(&child_vmar->object);

cleanup:
    virt_unlock();

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

void* vmar_allocate_bump(vmar_t* vmar, size_t size) {
    virt_lock();

    // check that we can expand the top without going over the max
    void* top = vmar->bump_alloc_top;
    if (top + size > vmar->bump_alloc_max) {
        virt_unlock();
        return NULL;
    }

    // update the top
    vmar->bump_alloc_top += size;

    virt_unlock();

    return top;
}

vmar_region_t* vmar_find_object(vmar_t* vmar, void* ptr) {
    for (;;) {
        // ensure its even in the range
        if (!vmar_contains_ptr(vmar, ptr)) {
            return NULL;
        }

        // check if its in the bump allocator
        if (vmar->bump_alloc_top != vmar->region.start) {
            if (ptr < vmar->bump_alloc_top) {
                return &vmar->region;
            }
        }

        // search for the children, assume the access
        // size is of at least 1 byte
        vmar_search_overlap_t key = {
            .start = ptr,
            .end = ptr + 1
        };
        struct rb_node* node = rb_find(&key, &vmar->root, vmar_cmp_overlap);
        if (node == NULL) {
            return NULL;
        }

        // get the region, we are going to lock it before we actually use it
        vmar_region_t* region = containerof(node, vmar_region_t, node);

        // if we got a VMAR then we need to search it for a child
        if (region->object->type == OBJECT_TYPE_VMAR) {
            vmar = containerof(region->object, vmar_t, object);

        } else {
            // otherwise we found the region
            return region;
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

    virt_lock();

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
    CHECK(vmo_top_address <= vmo_size);

    // allocate the vmar object
    vmar_region_t* region = alloc_type(vmar_region_t);
    CHECK_ERROR(region != NULL, ERROR_OUT_OF_MEMORY);
    region->page_offset = vmo_offset / PAGE_SIZE;
    region->exec = (options & VMAR_MAP_EXECUTE);
    region->write = (options & VMAR_MAP_WRITE);
    region->object = &vmo->object;

    // allocate a region to be used
    RETHROW(vmar_allocate_region(vmar, region, options & VMAR_MAP_SPECIFIC, vmar_offset, len, order));

    // if we want to page it in right now then do that
    if (options & VMAR_MAP_POPULATE) {
        RETHROW(virt_map_and_populate_vmo(region));
    }

    // finally we can commit the region
    vmar_commit_region(vmar, region);

    // output the mapped address
    if (mapped_addr != NULL) {
        *mapped_addr = region->start;
    }

cleanup:
    if (IS_ERROR(err)) {
        free_type(vmar_region_t, region);
    }

    virt_unlock();

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

static void vmar_print_tree_rec(vmar_region_t* region, char* prefix, size_t plen, bool is_last) {
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
            vmar_region_t* region = containerof(n, vmar_region_t, node);
            vmar_print_tree_rec(region, prefix, new_plen, rb_next(n) == NULL);
        }

        prefix[plen] = '\0';
    }
}

void vmar_print(vmar_t* vmar) {
    virt_lock();
    char prefix[256] = {0};
    vmar_print_tree_rec(&vmar->region, prefix, 0, true);
    virt_unlock();
}
