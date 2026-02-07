#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "arch/paging.h"
#include "lib/except.h"
#include "lib/rbtree/rbtree_types.h"

/**
 * The type of the mapping we have
 */
typedef enum region_type : uint8_t {
    /**
     * This is just a normal region
     */
    REGION_TYPE_DEFAULT,

    /**
     * This region maps physical memory
     */
    REGION_TYPE_MAPPING_PHYS,

    /**
     * This region maps allocated memory
     */
    REGION_TYPE_MAPPING_ALLOC,

    /**
     * This is a special mapping
     */
    REGION_TYPE_MAPPING_SPECIAL,
} region_type_t;

/**
 * The protections of the mapping
 */
typedef enum mapping_protection : uint8_t {
    /**
     * This is a read-write region
     */
    MAPPING_PROTECTION_RW,

    /**
     * This is a read-only region
     */
    MAPPING_PROTECTION_RO,

    /**
     * This is a read-execute region
     */
    MAPPING_PROTECTION_RX,
} mapping_protection_t;

/**
 * The caching policy for the mapping
 */
typedef enum mapping_cache_policy : uint8_t {
    /**
     * Used to map normal memory
     * - on x86-64 uses write-back
     */
    MAPPING_CACHE_POLICY_CACHED,

    /**
     * Used to map framebuffer memory
     * - on x86-64 uses write-combining
     */
    MAPPING_CACHE_POLICY_FRAMEBUFFER,

    /**
     * Used to map PCIe bars marked as prefetchable
     * - on x86-64 uses write-through
     */
    MAPPING_CACHE_POLICY_PREFETCHABLE,

    /**
     * Used to map uncached memory
     * - on x86-64 uses UC
     */
    MAPPING_CACHE_POLICY_UNCACHED,
} mapping_cache_policy_t;

typedef struct region {
    /**
     * Node in the RBTree of addresses
     */
    rb_node_t node;

    union {
        /**
         * The root (if this is aregion)
         */
        rb_root_t root;

        /**
         * The physical address of the mapping
         */
        uintptr_t phys;
    };

    /**
     * A debug name for this vmar, just to easily identify what its part of
     */
    const char* name;

    /**
     * The mapped base address
     */
    void* base;

    /**
     * The amount of pages mapped
     */
    size_t page_count;

    /**
     * The type of the region
     */
    region_type_t type;

    /**
     * The caching policy of the mapping
     */
    mapping_cache_policy_t cache_policy;

    /**
     * The protection of the mapping
     */
    mapping_protection_t protection;

    /**
     * Is this region locked, meaning it can't be changed
     */
    bool locked;

    /**
     * Is this region pinned, means it can be unmapped
     */
    bool pinned;
} region_t;

static inline void* region_end(region_t* region) {
    size_t size = PAGES_TO_SIZE(region->page_count) - 1;
    return region->base + size;
}

/**
 * Reserve a memory region inside the given memory region.
 *
 * @param parent_region [IN] The region to reserve inside
 * @param child_region  [IN] The new region structure that we are adding
 * @param order         [IN] The alignment, as `log2(alignment) - PAGE_SHIFT`
 * @returns false if out of memory
 */
bool region_reserve_static(region_t* parent_region, region_t* child_region, size_t order);

/**
 * Reserve a memory region inside the given memory region.
 *
 * @param parent        [IN] The region to reserve inside
 * @param page_count    [IN] The amount of pages to reserve
 * @param order         [IN] The alignment, as `log2(alignment) - PAGE_SHIFT`
 * @param addr          [IN] The address to map it at, or NULL if any
 * @returns NULL if out of space
 */
region_t* region_reserve(region_t* parent, size_t page_count, size_t order, void* addr);

/**
 * Initialize the memory regions allocator
 */
void init_region_alloc(void);

/**
 * Allocate memory inside the given region
 *
 * If the addr is NULL, will choose where to map on its own.
 *
 * If the address is not inside the region will fail
 *
 * @param region        [IN] The region to map inside of
 * @param page_count    [IN] The amount of pages to map
 * @param order         [IN] The alignment, as `log2(alignment) - PAGE_SHIFT`
 * @param addr          [IN] The address to map it at, or NULL if any
 * @returns NULL if out of memory
 */
region_t* region_allocate(region_t* region, size_t page_count, size_t order, void* addr);

/**
 * Map the given physical memory inside the given region
 *
 * If the addr is NULL, will choose where to map on its own.
 *
 * If the address is not inside the region will fail
 *
 * @param region        [IN] The region to map inside of
 * @param phys          [IN] The physical address to map
 * @param cache         [IN] The caching policy to use for the mapping
 * @param page_count    [IN] The amount of pages to map
 * @param addr          [IN] The address to map it at, or NULL if any
 * @returns NULL if out of memory
 */
region_t* region_map_phys(region_t* region, uint64_t phys, mapping_cache_policy_t cache, size_t page_count, void* addr);

/**
 * Protect the memory mapping at the given address
 *
 * NOTE: this assumes the memory is a user address
 *
 * NOTE: once something is set to locked it can't be protected again
 *
 * @param addr          [IN] The address to search for, must be exact
 * @param protection    [IN] The protection to set
 */
void mapping_protect(void* addr, mapping_protection_t protection);

/**
 * Allocate user stack with guard pages around it
 */
region_t* region_allocate_user_stack(const char* name, size_t stack_size);

/**
 * Free the entire region, also freeing all regions under it
 *
 * @param region        [IN] The region to free and unmap
 */
void region_free(region_t* region);

/**
 * Search for a mapping of the region
 *
 * @param region    [IN] The region to search inside of
 * @param addr      [IN] The address to search
 * @returns NULL if not found
 */
region_t* region_find_mapping(region_t* region, void* addr);

/**
 * Dump a region
 */
void region_dump(region_t* region);
