#pragma once
#include "arch/paging.h"
#include "lib/assert.h"
#include "lib/list.h"
#include "object/object.h"
#include "sync/spinlock.h"

typedef enum vmo_cache_policy : uint8_t {
    /**
     * Use hardware caching
     */
    VMO_CACHE_POLICY_CACHED,

    /**
     * Disable caching
     */
    VMO_CACHE_POLICY_UNCACHED,

    /**
     * Uncached with write combining
     */
    VMO_CACHE_POLICY_WRITE_COMBINING,
} vmo_cache_policy_t;

typedef enum vmo_type {
    /**
     * This is a normal VMO
     */
    VMO_TYPE_NORMAL,

    /**
     * This VMO represent a
     * physical range
     */
    VMO_TYPE_PHYSICAL,
} vmo_type_t;

/**
 * Mask for the frame of the vmo
 */
#define VMO_PAGE_FRAME_MASK     ((~((uint64_t)PAGE_MASK)) >> PAGE_SHIFT)

/**
 * Is there even a page in the vmo
 */
#define VMO_PAGE_PRESENT        BIT63

/**
 * Virtual Memory Object
 */
typedef struct vmo {
    /**
     * The base object
     */
    object_t object;

    /**
     * The cache policy that should be used for the mappings
     */
    vmo_cache_policy_t cache_policy;

    /**
     * The type of the vmo
     */
    vmo_type_t type;

    /**
     * The amount of pages that are part of this VMO
     */
    size_t page_count;

    /**
     * For physical VMO this only has the base address
     * For normal VMO this has the entire range
     */
    uint64_t pages[];
} vmo_t;

static inline vmo_t* vmo_get(vmo_t* vmo) { object_get(&vmo->object); return vmo; }
static inline void vmo_put(vmo_t* vmo) { object_put(&vmo->object); }

/**
 * Create a VMO of anonymous memory
 *
 * @param size  [IN] The size to create
 * @return The VMO or NULL if out of memory
 */
vmo_t* vmo_create(uint64_t size);

/**
 * Create a VMO of specific physical memory
 *
 * @param physical_address  [IN]
 * @param size              [IN]
 * @return The VMO or NULL if out of memory
 */
vmo_t* vmo_create_physical(uint64_t physical_address, size_t size);

/**
 * Get the size in bytes of the VMO
 */
static inline size_t vmo_get_size(vmo_t* vmo) {
    return vmo->page_count * PAGE_SIZE;
}
