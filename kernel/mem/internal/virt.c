#include "virt.h"

#include "memory.h"
#include "phys.h"
#include "arch/paging.h"
#include "lib/string.h"
#include "sync/spinlock.h"

/**
 * Spinlock for mapping virtual pages
 */
static irq_spinlock_t m_virt_lock = IRQ_SPINLOCK_INIT;

/**
 * The kernel top level cr3
 */
static uint64_t* m_pml4 = 0;

bool virt_is_mapped(uintptr_t virt) {
    size_t index4 = (virt >> 39) & PAGING_INDEX_MASK;
    size_t index3 = (virt >> 30) & PAGING_INDEX_MASK;
    size_t index2 = (virt >> 21) & PAGING_INDEX_MASK;
    size_t index1 = (virt >> 12) & PAGING_INDEX_MASK;

    uint64_t* pml4 = PHYS_TO_DIRECT(__readcr3() & PAGING_4K_ADDRESS_MASK);

    uint64_t pml4e = pml4[index4];
    if ((pml4e & IA32_PG_P) == 0) return false;
    uint64_t* pml3 = PHYS_TO_DIRECT(pml4e & PAGING_4K_ADDRESS_MASK);

    uint64_t pml3e = pml3[index3];
    if ((pml3e & IA32_PG_P) == 0) return false;
    if (pml3e & IA32_PG_PS) return true; // 1gb page
    uint64_t* pml2 = PHYS_TO_DIRECT(pml3e & PAGING_4K_ADDRESS_MASK);

    uint64_t pml2e = pml2[index2];
    if ((pml2e & IA32_PG_P) == 0) return false;
    if (pml2e & IA32_PG_PS) return true; // 2mb page
    uint64_t* pml1 = PHYS_TO_DIRECT(pml2e & PAGING_4K_ADDRESS_MASK);

    uint64_t pml1e = pml1[index1];
    if ((pml1e & IA32_PG_P) == 0) return false;

    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Strict ops
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static err_t virt_fail_on_already_mapped(struct map_ops* ops, void* virt, uint64_t phys) {
    err_t err = NO_ERROR;

    CHECK_FAIL("Attempted to map a page that is already mapped");

    cleanup:
        return err;
}

map_ops_t g_virt_map_strict_ops = {
    .mapped_page = virt_fail_on_already_mapped,
};

static err_t virt_fail_unmap_not_present(struct unmap_ops* ops, void* virt) {
    err_t err = NO_ERROR;

    CHECK_FAIL("Attempted to unmap a page that is not present");

cleanup:
    return err;
}

unmap_ops_t g_virt_unmap_strict_ops = {
    .unmapped_page = virt_fail_unmap_not_present,
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Map/Unmap primitives
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint64_t* virt_get_next_level(uint64_t* entry, bool allocate) {
    // ensure we don't have a large page in the way
    ASSERT((*entry & IA32_PG_PS) == 0);

    if ((*entry & IA32_PG_P) == 0) {
        if (!allocate) {
            return NULL;
        }

        void* phys = phys_alloc(PAGE_SIZE);
        if (phys == NULL) {
            return NULL;
        }
        memset(phys, 0, PAGE_SIZE);

        *entry = DIRECT_TO_PHYS(phys) | IA32_PG_P | IA32_PG_RW;
    }

    return PHYS_TO_DIRECT(*entry & PAGING_4K_ADDRESS_MASK);
}

static uint64_t* virt_get_pte(void* virt, bool allocate) {
    size_t index4 = ((uintptr_t)virt >> 39) & PAGING_INDEX_MASK;
    size_t index3 = ((uintptr_t)virt >> 30) & PAGING_INDEX_MASK;
    size_t index2 = ((uintptr_t)virt >> 21) & PAGING_INDEX_MASK;
    size_t index1 = ((uintptr_t)virt >> 12) & PAGING_INDEX_MASK;

    uint64_t* pml3 = virt_get_next_level(&m_pml4[index4], allocate);
    if (pml3 == NULL) {
        return NULL;
    }

    uint64_t* pml2 = virt_get_next_level(&pml3[index3], allocate);
    if (pml2 == NULL) {
        return NULL;
    }

    uint64_t* pml1 = virt_get_next_level(&pml2[index2], allocate);
    if (pml1 == NULL) {
        return NULL;
    }

    return &pml1[index1];
}

static err_t virt_map_locked(void* virt, uint64_t phys, size_t num_pages, map_flags_t flags, map_ops_t* ops) {
    err_t err = NO_ERROR;

    bool need_tlb_shootdown = false;

    for (size_t i = 0; i < num_pages; i++, virt += PAGE_SIZE, phys += PAGE_SIZE) {
        uint64_t *pte = virt_get_pte(virt, true);
        CHECK_ERROR(pte != NULL, ERROR_OUT_OF_MEMORY);

        // set the entry as requested
        uint64_t entry = phys | IA32_PG_P | IA32_PG_D | IA32_PG_A;
        if ((flags & MAP_FLAG_EXECUTABLE) == 0) entry |= IA32_PG_NX;
        if ((flags & MAP_FLAG_WRITEABLE) != 0) entry |= IA32_PG_RW;
        if ((flags & MAP_FLAG_UNCACHEABLE) != 0) entry |= IA32_PG_CACHE_UC;

        if (ops != NULL) {
            if (*pte == entry) {
                // we mapped the same entry again
                if (ops->mapped_same_entry != NULL) {
                    RETHROW(ops->mapped_same_entry(ops, virt, *pte & PAGING_4K_ADDRESS_MASK));
                }
            } else if (*pte & IA32_PG_P) {
                // we are mapping with a different entry
                if (ops->mapped_page != NULL) {
                    RETHROW(ops->mapped_page(ops, virt, *pte & PAGING_4K_ADDRESS_MASK));
                }
            }
        }

        // if we are remapping an existing entry then issue a shootdown
        if ((entry & IA32_PG_P) && *pte != entry) {
            need_tlb_shootdown = true;
            __invlpg(virt);
        }

        // and set it
        *pte = entry;
    }

    if (need_tlb_shootdown) {
        // TODO: actually issue the shootdown
    }

cleanup:
    return err;
}

err_t virt_map(void* virt, uint64_t phys, size_t num_pages, map_flags_t flags, map_ops_t* ops) {
    err_t err = NO_ERROR;

    bool irq_state = irq_spinlock_acquire(&m_virt_lock);
    unlock_direct_map();

    RETHROW(virt_map_locked(virt, phys, num_pages, flags, ops));

cleanup:
    lock_direct_map();
    irq_spinlock_release(&m_virt_lock, irq_state);

    return err;
}

err_t virt_unmap(void* virt, size_t num_pages, unmap_ops_t* ops) {
    err_t err = NO_ERROR;

    bool irq_state = irq_spinlock_acquire(&m_virt_lock);
    unlock_direct_map();

    for (size_t i = 0; i < num_pages; i++, virt += PAGE_SIZE) {
        uint64_t* pte = virt_get_pte(virt, false);
        if (pte == NULL) {
            if (ops && ops->unmapped_page) {
                RETHROW(ops->unmapped_page(ops, virt));
            }
            continue;
        }

        // must already be mapped
        if (ops != NULL) {
            if (*pte & IA32_PG_P) {
                if (ops->mapped_page) {
                    RETHROW(ops->mapped_page(ops, virt, *pte & PAGING_4K_ADDRESS_MASK));
                }
            } else {
                if (ops->unmapped_page) {
                    RETHROW(ops->unmapped_page(ops, virt));
                }
            }
        }

        // just remove the entire page entry
        *pte = 0;
        __invlpg(virt);
    }

    // TODO: queue TLB invalidation on all other cores

cleanup:
    lock_direct_map();
    irq_spinlock_release(&m_virt_lock, irq_state);

    return err;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialization
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

err_t init_virt(void) {
    // just save the pml4 from the early virt init so we can
    // switch on other cores nicely
    m_pml4 = PHYS_TO_DIRECT(__readcr3() & PAGING_4K_ADDRESS_MASK);

    return NO_ERROR;
}

void switch_page_table(void) {
    // switch to the page table
    __writecr3(DIRECT_TO_PHYS(m_pml4));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Virtual range allocation
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

err_t virt_alloc(void* ptr, size_t num_pages) {
    err_t err = NO_ERROR;

    for (; num_pages--; ptr += PAGE_SIZE) {
        // allocate the given page
        void* page = phys_alloc(PAGE_SIZE);
        CHECK_ERROR(page != NULL, ERROR_OUT_OF_MEMORY);

        // unmap from the direct map
        RETHROW(virt_unmap(page, 1, VIRT_UNMAP_STRICT));

        // map into the wanted virtual address
        RETHROW(virt_map(ptr, DIRECT_TO_PHYS(page), 1, MAP_FLAG_WRITEABLE, VIRT_MAP_STRICT));
    }

cleanup:
    if (IS_ERROR(err)) {
        ASSERT(!"TODO: free the already allocated range");
    }

    return err;
}

static err_t virt_free_page(unmap_ops_t* ops, void* virt, uintptr_t phys) {
    // remap the page into the direct map
    // this should never fail because we have already mapped it once
    ASSERT(!IS_ERROR(virt_map_locked(PHYS_TO_DIRECT(phys), phys, 1, MAP_FLAG_WRITEABLE, VIRT_MAP_STRICT)));

    // return it to the physical allocator
    phys_free(PHYS_TO_DIRECT(phys), PAGE_SIZE);

    // we need to unlock the direct map again since the phys_free
    // will lock it (and we don't have nesting support)
    unlock_direct_map();

    return NO_ERROR;
}

/**
 * These operations will fail on an unmapped page
 * and free an already mapped page
 */
static unmap_ops_t m_virt_free_ops = {
    .mapped_page = virt_free_page,
    .unmapped_page = virt_fail_unmap_not_present
};

void virt_free(void* ptr, size_t num_pages) {
    // this should never fail in normal runtime
    ASSERT(!IS_ERROR(virt_unmap(ptr, num_pages, &m_virt_free_ops)));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Page fault handling
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool virt_handle_page_fault(uintptr_t addr) {
    return false;
}
