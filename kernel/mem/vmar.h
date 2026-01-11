#pragma once

#include "mapping.h"
#include "vmo.h"
#include "lib/except.h"
#include "object/object.h"
#include "sync/spinlock.h"

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
    mapping_t region;

    /**
     * The root for this region entries
     */
    struct rb_root root;
} vmar_t;

static inline bool vmar_contains_ptr(vmar_t* vmar, void* ptr) {
    return vmar->region.start <= ptr && ptr <= vmar->region.start + (PAGES_TO_SIZE(vmar->region.page_count) - 1);
}

static inline vmar_t* vmar_get(vmar_t* vmar) { object_get(&vmar->object); return vmar; }
static inline void vmar_put(vmar_t* vmar) { object_put(&vmar->object); }

typedef enum vmar_options {
    /**
     * Allocate the vmar at a specific address
     */
    VMAR_SPECIFIC = BIT0,
} vmar_options_t;

/**
 * Allocate a new VMAR from a statically allocated VMAR struct.
 *
 * Used during init
 *
 * @param parent_vmar   [IN] The parent VMAR
 * @param child_vmar    [IN] An already allocated VMAR to be added
 * @param options       [IN] Options for the allocation
 * @param offset        [IN] When VMAR_SPECIFIC is used the offset from the base of the parent VMAR to map at
 * @param size          [IN] The size of the VMAR to reserve
 * @param order         [IN] The alignment, as `log2(alignment) - 12`
 * @return
 */
err_t vmar_allocate_static(
    vmar_t* parent_vmar,
    vmar_t* child_vmar,
    vmar_options_t options,
    size_t offset, size_t size, size_t order
);

/**
 * Allocate a new VMAR
 *
 * @param parent_vmar   [IN]    The parent VMAR
 * @param options       [IN]    Options for the allocation
 * @param offset        [IN]    When VMAR_SPECIFIC is used the offset from the base of the parent VMAR to map at
 * @param size          [IN]    The size of the VMAR to reserve
 * @param order         [IN]    The alignment, as `log2(alignment) - 12`
 * @param child_vmar    [OUT]   The new VMAR object
 */
err_t vmar_allocate(
    vmar_t* parent_vmar,
    vmar_options_t options,
    size_t offset, size_t size, size_t order,
    vmar_t** child_vmar
);

typedef enum vmar_map_options {
    /**
     * Map at a specific address
     */
    VMAR_MAP_SPECIFIC = BIT0,

    /**
     * Map as writable memory
     */
    VMAR_MAP_WRITE = BIT1,

    /**
     * Map as executable memory
     */
    VMAR_MAP_EXECUTE = BIT2,

    /**
     *  Populate the entire range right away
     */
    VMAR_MAP_POPULATE = BIT3,
} vmar_map_options_t;

/**
 * Map the given vmo
 *
 * If succeeds takes the ref
 *
 * @param vmar          [IN]    The VMAR to map into
 * @param options       [IN]    The options for the mapping
 * @param vmar_offset   [IN]    The offset in the vmar to map from, used with VMAR_MAP_SPECIFIC
 * @param vmo           [IN]    The vmo to map
 * @param vmo_offset    [IN]    The offset in the vmo to map, must be page aligned
 * @param len           [IN]    The amount to map, must be page aligned
 * @param order         [IN]    The alignment, as `log2(alignment) - 12`
 * @param mapped_addr   [OUT]   The address that the vmo was mapped at
 */
err_t vmar_map(
    vmar_t* vmar,
    vmar_map_options_t options,
    size_t vmar_offset,
    vmo_t* vmo,
    size_t vmo_offset, size_t len, size_t order,
    void** mapped_addr
);

/**
 * Unmaps all VMO mappings and destroys all sub-regions within the
 * given range
 *
 * @param vmar  [IN] The VMAR to unmap from
 * @param addr  [IN] The address to unmap from
 * @param len   [IN] The length to unmap
 */
void vmar_unmap(vmar_t* vmar, void* addr, size_t len);

/**
 * Attempt to find a mapped VMO in the given vmar
 *
 * Will return the region that should have a mapped object.
 *
 * The object will always be a VMO
 *
 * The virt lock must be taken at this point
 *
 * @param vmar  [IN]    The VMAR to search from
 * @param ptr   [IN]    The pointer to search for
 */
mapping_t* vmar_find_mapping(vmar_t* vmar, void* ptr);

/**
 * Pretty print a vmar tree
 */
void vmar_print(vmar_t* vmar);
