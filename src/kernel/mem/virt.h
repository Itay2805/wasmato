#pragma once

#include <stdbool.h>

#include "arch/intr.h"
#include "lib/defs.h"
#include "lib/except.h"
#include "uapi/mapping.h"

/**
 * Normal init, setting up the page tables before we can switch to them
 */
INIT_CODE err_t init_virt(void);

/**
 * Switch to the kernel's page table
 */
INIT_CODE void switch_page_table(void);

static inline void user_access_enable(void) {
    asm("stac");
}

static inline void user_access_disable(void) {
    asm("clac");
}

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
 * Umap all pages under the range
 *
 * @param virt          [IN] The start address
 * @param page_count    [IN] The page count
 * @param free          [IN] Should we also free the physical pages
 */
void virt_unmap(void* virt, size_t page_count, bool free);

/**
 * Handle a TLB flush ipi
 */
void virt_handle_tlb_flush_ipi(void);

/**
 * Attempt to handle a page fault for lazy-memory allocation
 */
err_t virt_handle_page_fault(uintptr_t addr, uint32_t code);

/**
 * Reprotect pages in .rodata.late as readonly, so they can't change anymore
 */
void protect_ro_data(void);

/**
 * Unmap and free all pages in the .text.init section, reclaiming them to the
 * physical allocator. Must be called exactly once, after all CPUs have
 * finished executing init code.
 */
void reclaim_init_mem(void);
