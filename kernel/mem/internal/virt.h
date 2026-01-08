#pragma once

#include <stdbool.h>

#include "lib/defs.h"
#include "lib/except.h"

typedef enum map_flags {
    /**
     * The flag should be writable
     */
    MAP_FLAG_WRITEABLE = BIT0,

    /**
     * The page should be executable
     */
    MAP_FLAG_EXECUTABLE = BIT1,

    /**
     * The page should be uncached
     */
    MAP_FLAG_UNCACHEABLE = BIT2,
} map_flags_t;

/**
 * Normal init, setting up the page tables before we can switch to them
 */
err_t init_virt(void);

/**
 * Switch to the kernel's page table
 */
void switch_page_table(void);

/**
 * Check if an address is mapped currently, using the actual
 * page tables, should be used mainly for debugging and fault
 * handling
 */
bool virt_is_mapped(uintptr_t virt);

typedef struct map_ops {
    /**
     * Callback when mapping a page that is already mapped
     */
    err_t (*mapped_page)(struct map_ops* ops, void* virt, uint64_t phys);

    /**
     * Callback when mapping the same exact entry again (same flags, same physical address)
     */
    err_t (*mapped_same_entry)(struct map_ops* ops, void* virt, uint64_t phys);
} map_ops_t;

extern map_ops_t g_virt_map_strict_ops;
#define VIRT_MAP_STRICT &g_virt_map_strict_ops

/**
 * Map the given range of pages
 */
err_t virt_map(void* virt, uint64_t phys, size_t num_pages, map_flags_t flags, map_ops_t* ops);

typedef struct unmap_ops {
    /**
     * Callback before unmapping a mapped page
     */
    err_t (*mapped_page)(struct unmap_ops* ops, void* virt, uintptr_t phys);

    /**
     * Callback when finding a page which is not mapped
     */
    err_t (*unmapped_page)(struct unmap_ops* ops, void* virt);
} unmap_ops_t;

extern unmap_ops_t g_virt_unmap_strict_ops;
#define VIRT_UNMAP_STRICT &g_virt_unmap_strict_ops

/**
 * Unmap the given range
 */
err_t virt_unmap(void* virt, size_t num_pages, unmap_ops_t* ops);

/**
 * Allocate and map a range
 */
err_t virt_alloc(void* ptr, size_t num_pages);

/**
 * Free the given range
 */
void virt_free(void* ptr, size_t num_pages);

/**
 * Attempt to handle a page fault for lazy-memory allocation
 */
bool virt_handle_page_fault(uintptr_t addr);
