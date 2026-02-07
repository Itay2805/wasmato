#include "virt.h"

#include "direct.h"
#include "phys.h"
#include "arch/gdt.h"
#include "arch/intr.h"
#include "arch/paging.h"
#include "lib/string.h"
#include "sync/spinlock.h"
#include "thread/pcpu.h"

/**
 * Spinlock to protect the page table modification
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

    uint64_t* pml4 = phys_to_direct(__readcr3() & PAGING_4K_ADDRESS_MASK);

    uint64_t pml4e = pml4[index4];
    if ((pml4e & IA32_PG_P) == 0) return false;
    uint64_t* pml3 = phys_to_direct(pml4e & PAGING_4K_ADDRESS_MASK);

    uint64_t pml3e = pml3[index3];
    if ((pml3e & IA32_PG_P) == 0) return false;
    if (pml3e & IA32_PG_PS) return true; // 1gb page
    uint64_t* pml2 = phys_to_direct(pml3e & PAGING_4K_ADDRESS_MASK);

    uint64_t pml2e = pml2[index2];
    if ((pml2e & IA32_PG_P) == 0) return false;
    if (pml2e & IA32_PG_PS) return true; // 2mb page
    uint64_t* pml1 = phys_to_direct(pml2e & PAGING_4K_ADDRESS_MASK);

    uint64_t pml1e = pml1[index1];
    if ((pml1e & IA32_PG_P) == 0) return false;

    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialization
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

err_t init_virt(void) {
    // just save the pml4 from the early virt init so we can
    // switch on other cores nicely
    m_pml4 = phys_to_direct(__readcr3() & PAGING_4K_ADDRESS_MASK);

    return NO_ERROR;
}

void switch_page_table(void) {
    // switch to the page table
    __writecr3(direct_to_phys(m_pml4));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mapping utilities
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

        *entry = direct_to_phys(phys) | IA32_PG_P | IA32_PG_RW;
    }

    return phys_to_direct(*entry & PAGING_4K_ADDRESS_MASK);
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Page fault handling
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

err_t virt_handle_page_fault(uintptr_t addr, uint32_t code, bool kernel) {
    err_t err = NO_ERROR;

    bool irq_state = irq_spinlock_acquire(&m_virt_lock);

    // these are the only accesses that could make sense for our handler
    uint32_t allowed_mask = IA32_PF_EC_WRITE | IA32_PF_EC_USER;
    CHECK((code & allowed_mask) == code);

    // get the region it happened in
    region_t* region = NULL;
    if (kernel) {
        CHECK(g_kernel_memory.base <= (void*)addr);
        region = &g_kernel_memory;
        CHECK((code & IA32_PF_EC_USER) == 0);
    } else {
        CHECK((void*)addr <= g_user_memory.base);
        region = &g_user_memory;
        CHECK(code & IA32_PF_EC_USER);
    }

    // search for the actual mapping where we faulted
    region = region_find_mapping(region, (void*)addr);
    CHECK(region != NULL);

    // we don't expect executable pages to ever fault, since at the time
    // we get to them they should be setup properly
    CHECK(region->protection != MAPPING_PROTECTION_RX);

    // if we have a read-only page, don't allow to fault on write
    if (region->protection == MAPPING_PROTECTION_RO) {
        CHECK((code & IA32_PF_EC_WRITE) == 0);
    }

    // get the pte, we assume it was not allocated yet
    // TODO: when we support protection faults we should assume its already allocated
    uint64_t* pte = virt_get_pte((void*)addr, true);
    CHECK(pte != NULL);
    CHECK(*pte == 0);

    // we can now actually do stuff
    uint64_t phys;
    if (region->type == REGION_TYPE_MAPPING_PHYS) {
        // TODO: map this nicely, and maybe even try to use larger mappings
        //       when possible
        CHECK_FAIL("TODO: this");

    } else if (region->type == REGION_TYPE_MAPPING_ALLOC) {
        // allocate the page
        void* page = phys_alloc(PAGE_SIZE);
        CHECK(page != NULL);
        phys = direct_to_phys(page);

    } else {
        // invalid type
        CHECK_FAIL();
    }

    // setup the pte, we assume it can't be executable so mark as NX right away
    uint64_t new_pte = phys | IA32_PG_P | IA32_PG_NX | IA32_PG_A | IA32_PG_D;
    if (!kernel) new_pte |= IA32_PG_U;
    if (region->protection == MAPPING_PROTECTION_RW) new_pte |= IA32_PG_RW;

    // setup caching
    switch (region->cache_policy) {
        case MAPPING_CACHE_POLICY_CACHED: new_pte |= IA32_PG_CACHE_WB; break;
        case MAPPING_CACHE_POLICY_FRAMEBUFFER: new_pte |= IA32_PG_CACHE_WC_4K; break;
        case MAPPING_CACHE_POLICY_PREFETCHABLE: new_pte |= IA32_PG_CACHE_WT; break;
        case MAPPING_CACHE_POLICY_UNCACHED: new_pte |= IA32_PG_CACHE_UCM; break;
        default: CHECK_FAIL();
    }

    // and set it
    *pte = new_pte;

cleanup:
    if (IS_ERROR(err)) {
        if (kernel) {
            region_dump(&g_kernel_memory);
        } else {
            region_dump(&g_user_memory);
        }
    }

    irq_spinlock_release(&m_virt_lock, irq_state);

    return err;
}
