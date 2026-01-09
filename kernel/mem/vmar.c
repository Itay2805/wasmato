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

static void* vmar_can_place_in_gap(void* gap_start, void* gap_end, size_t size, size_t align) {
    if ((gap_end - gap_start) + 1 < size) {
        return NULL;
    }

    void* start = (void*)ALIGN_DOWN((gap_end - size) + 1, align);
    if (start <= gap_start) {
        return NULL;
    }

    return start;
}

static void* vmar_find_empty_region(vmar_t* vmar, size_t size, size_t align) {
    size = ALIGN_UP(size, PAGE_SIZE);

    // the highest possible end
    void* gap_end = vmar->region.end;

    void* start = NULL;
    for (struct rb_node* n = rb_last(&vmar->root); n != NULL; n = rb_prev(n)) {
        vmar_region_t* region = rb_entry(n, vmar_region_t, node);

        // if this is not at the same place then check it out
        // this ensures the end + 1 won't overflow
        if (region->end != gap_end) {
            // can start from the end of this region
            // to the start of the last region
            void* gap_start = region->end + 1;

            // attempt to place inside of this gap
            start = vmar_can_place_in_gap(gap_start, gap_end, size, align);
            if (start != NULL) {
                break;
            }
        }

        // Next end is this region's start
        gap_end = region->start - 1;
    }

    // attempt to place between the start and the first entry
    if (start == NULL) {
        start = vmar_can_place_in_gap(vmar->bump_alloc_top, gap_end, size, align);
        if (start == NULL) {
            // failed to find a good region
            return NULL;
        }
    }

    return start;
}

typedef struct vmar_search_overlap {
    void* start;
    void* end;
} vmar_search_overlap_t;

static int vmar_cmp_overlap(const void* key, const struct rb_node* node) {
    const vmar_search_overlap_t* ctx = key;
    const vmar_region_t* region = containerof(node, vmar_region_t, node);

    // if overlaps then
    TRACE("%p", ctx);
    TRACE("%p", region);
    if (ctx->end >= region->start && region->end >= ctx->start) {
        return 0;
    }

    // otherwise let it search
    if (ctx->start < region->start) {
        return -1;
    } else {
        return 1;
    }
}

static void vmar_add_region(vmar_t* vmar, vmar_region_t* region, void* start, size_t size) {
    // set the region fields and add the to rbtree
    region->start = start;
    region->end = start + (size - 1);
    rb_add(&region->node, &vmar->root, vmar_region_less);

    // update the top address of the bump allocator
    if (region->start < vmar->bump_alloc_max) {
        vmar->bump_alloc_max = region->start;
    }
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

    // ensure the size is page aligned
    CHECK((size % PAGE_SIZE) == 0);
    CHECK(size > 0);

    // the alignment that we have
    size_t alignment = 1ul << (order + 12);

    // search for an empty region where we can create the region
    void* start = NULL;
    if (options & VMAR_SPECIFIC) {
        CHECK((offset % PAGE_SIZE) == 0);

        // the start we want, ensure the alignment is correct
        start = parent_vmar->region.start + offset;
        CHECK(((uintptr_t)start % alignment) == 0);

        // we must be above the top of the bump
        CHECK(start >= parent_vmar->bump_alloc_top);

        // ensure it doesn't overlap with anything else
        vmar_search_overlap_t key = {
            .start = start,
            .end = start + (size - 1)
        };
        CHECK(rb_find(&key, &parent_vmar->root, vmar_cmp_overlap) == NULL);

    } else {
        // carve space from the parent, when this returns with true we are already inside of the
        // address space and should be worried about being unmapped
        start = vmar_find_empty_region(parent_vmar, size, alignment);
        CHECK_ERROR(start != NULL, ERROR_OUT_OF_MEMORY);
    }

    // setup the region itself, we are going to take a ref to the object
    // so the parent vmar will keep it alive
    child_vmar->region.object = object_get(&child_vmar->object);
    vmar_add_region(parent_vmar, &child_vmar->region, start, size);

    // for making the code simpler we always initialize the top
    // of the bump allocator
    child_vmar->bump_alloc_top = child_vmar->region.start;

    // if we want to allow a bump allocator set the region end
    if (options & VMAR_CAN_BUMP) {
        child_vmar->bump_alloc_max = child_vmar->region.end + 1;
    } else {
        child_vmar->bump_alloc_max = child_vmar->bump_alloc_top;
    }

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
        struct rb_node* node = rb_find(ptr, &vmar->root, vmar_cmp_overlap);
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

void vmar_destroy(vmar_t* vmar) {
    // must already be unmapped at this point, given that the region holds
    // a ref on itself while being mapped
    ASSERT(vmar->region.start == NULL);
    ASSERT(vmar->region.end == NULL);
    free_type(vmar_t, vmar);
}

static void vmar_print_tree_rec(vmar_region_t* region, char* prefix, size_t plen, bool is_last) {
    if (plen) {
        debug_print("%s", prefix);
        debug_print("%s", is_last ? "└── " : "├── ");
    }

    // at this point we must be a vmar
    ASSERT(region->object->type == OBJECT_TYPE_VMAR);
    vmar_t* vmar = containerof(region->object, vmar_t, object);

    const char* name = region->object->name;
    if (name == NULL) {
        name = "";
    }
    debug_print("VMAR<%s>: %p-%p %lx\n", name, region->start, region->end, (region->end - region->start) + 1);

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

void vmar_print(vmar_t* vmar) {
    virt_lock();
    char prefix[256] = {0};
    vmar_print_tree_rec(&vmar->region, prefix, 0, true);
    virt_unlock();
}
