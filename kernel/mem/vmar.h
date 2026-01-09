#pragma once

#include "lib/except.h"
#include "object/object.h"
#include "lib/rbtree/rbtree.h"
#include "sync/spinlock.h"

/**
 * Represents a single vmar region
 */
typedef struct vmar_region {
    /**
     * The node in the VMAR that this region is part of
     */
    struct rb_node node;

    /**
     * The object that this region represents
     */
    object_t* object;

    /**
     * The address range that the region takes
     */
    void* start;
    void* end;
} vmar_region_t;

/**
 * Virtual Memory Address Region
 */
typedef struct vmar {
    /**
     * The base object
     */
    object_t object;

    /**
     * The region object that is used to be
     * part of the parent
     */
    vmar_region_t region;

    /**
     * The root for this region entries
     */
    struct rb_root root;

    /**
     * The bump allocator's current address
     */
    void* bump_alloc_top;

    /**
     * The bump allocators max address
     */
    void* bump_alloc_max;
} vmar_t;

static inline bool vmar_contains_ptr(vmar_t* vmar, void* ptr) {
    return vmar->region.start <= ptr && ptr <= vmar->region.end;
}

static inline vmar_t* vmar_get(vmar_t* vmar) { object_get(&vmar->object); return vmar; }
static inline void vmar_put(vmar_t* vmar) { object_put(&vmar->object); }

typedef enum vmar_options {
    /**
     * Allocate the vmar at a specific address
     */
    VMAR_SPECIFIC = BIT0,

    /**
     * Can the bump allocator be used
     */
    VMAR_CAN_BUMP = BIT1,
} vmar_options_t;

err_t vmar_allocate_static(
    vmar_t* parent_vmar,
    vmar_t* child_vmar,
    vmar_options_t options,
    size_t offset, size_t size, size_t order
);

err_t vmar_allocate(
    vmar_t* parent_vmar,
    vmar_options_t options,
    size_t offset, size_t size, size_t order,
    vmar_t** child_vmar
);

/**
 * Allocate from the bump allocator of the vmar, by the given amount of bytes.
 *
 * The memory of the bump allocator allocated on demand
 *
 * @param size          [IN] How much to allocate
 * @return The pointer to the start of the allocated area from the bump
 */
void* vmar_allocate_bump(vmar_t* vmar, size_t size);

/**
 * Attempt to find a mapped object in the given vmar
 *
 * Will return the region that should have a mapped object.
 *
 * For the bump allocator it will return the VMAR that contains
 * the allocator.
 *
 * The virt lock must be taken at this point
 *
 * @param vmar  [IN]    The VMAR to search from
 * @param ptr   [IN]    The pointer to search for
 */
vmar_region_t* vmar_find_object(vmar_t* vmar, void* ptr);

/**
 * Pretty print a vmar tree
 */
void vmar_print(vmar_t* vmar);
