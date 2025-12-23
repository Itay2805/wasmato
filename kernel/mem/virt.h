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

typedef enum protection_key {
    /**
     * The default key used for all pages
     */
    PROTECTION_KEY_DEFAULT,

    /**
     * The protection key
     */
    PROTECTION_KEY_PAGE_TABLE,
} protection_key_t;

/**
 * Check if protection keys are supported
 */
bool virt_pk_supported(void);

/**
 * Early init, before we have a physical memory allocator
 */
err_t init_virt_early();

/**
 * Normal init, setting up the page tables before we can switch to them
 */
err_t init_virt();

/**
 * Check if an address is mapped currently, using the actual
 * page tables, should be used mainly for debugging and fault
 * handling
 */
bool virt_is_mapped(uintptr_t virt);

/**
 * Map the given range of pages
 */
err_t virt_map(uintptr_t virt, uint64_t phys, size_t num_pages, map_flags_t flags);

/**
 * Unmap the given range
 */
err_t virt_unmap(uintptr_t virt, size_t num_pages);

/**
 * Switch to the kernel's page table
 */
void switch_page_table();

/**
 * Attempt to handle a page fault for lazy-memory allocation
 */
bool virt_handle_page_fault(uintptr_t addr);
