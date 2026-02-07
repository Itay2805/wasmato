#pragma once

#include <stdbool.h>

#include "arch/intr.h"
#include "lib/defs.h"
#include "lib/except.h"
#include "mem/region.h"

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
 * Change the protections of a given memory range
 *
 * @param virt          [IN] The virtual start address
 * @param page_count    [IN] The amount of pages to modify
 * @param protection    [IN] The new protections we want
 */
void virt_protect(void* virt, size_t page_count, mapping_protection_t protection);

/**
 * Attempt to handle a page fault for lazy-memory allocation
 */
err_t virt_handle_page_fault(uintptr_t addr, uint32_t code);
