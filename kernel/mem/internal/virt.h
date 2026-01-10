#pragma once

#include <stdbool.h>

#include "lib/defs.h"
#include "lib/except.h"
#include "mem/vmar.h"

/**
 * Unlock the virtual address space
 */
void virt_lock();

/**
 * Lock the virtual address space
 */
void virt_unlock(void);

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

/**
 * Map the given VMO at the given address
 *
 * NOTE: The virt lock must already be taken
 *
 * @param region    [IN] The region to map into
 */
err_t virt_map_and_populate_vmo(vmar_region_t* region);

/**
 * Attempt to handle a page fault for lazy-memory allocation
 */
err_t virt_handle_page_fault(uintptr_t addr, uint32_t code);
