#include "virt.h"

#include "direct.h"
#include "phys.h"
#include "arch/gdt.h"
#include "arch/intr.h"
#include "arch/paging.h"
#include "lib/string.h"
#include "sync/spinlock.h"

/**
 * Spinlock for mapping virtual pages
 */
static irq_spinlock_t m_virt_lock = IRQ_SPINLOCK_INIT;
static bool m_virt_lock_irq_state = false;

/**
 * The kernel top level cr3
 */
static uint64_t* m_pml4 = 0;

void virt_lock(void) {
    m_virt_lock_irq_state = irq_spinlock_acquire(&m_virt_lock);
}

void virt_unlock(void) {
    irq_spinlock_release(&m_virt_lock, m_virt_lock_irq_state);
}

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

static uint64_t* virt_get_next_level(uint64_t* entry, bool allocate, bool direct) {
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

        *entry = direct_to_phys(phys) | IA32_PG_P | IA32_PG_RW | (direct ? IA32_PG_U : 0);
    }

    return phys_to_direct(*entry & PAGING_4K_ADDRESS_MASK);
}

static uint64_t* virt_get_pte(void* virt, bool allocate, bool direct) {
    size_t index4 = ((uintptr_t)virt >> 39) & PAGING_INDEX_MASK;
    size_t index3 = ((uintptr_t)virt >> 30) & PAGING_INDEX_MASK;
    size_t index2 = ((uintptr_t)virt >> 21) & PAGING_INDEX_MASK;
    size_t index1 = ((uintptr_t)virt >> 12) & PAGING_INDEX_MASK;

    uint64_t* pml3 = virt_get_next_level(&m_pml4[index4], allocate, direct);
    if (pml3 == NULL) {
        return NULL;
    }

    uint64_t* pml2 = virt_get_next_level(&pml3[index3], allocate, direct);
    if (pml2 == NULL) {
        return NULL;
    }

    uint64_t* pml1 = virt_get_next_level(&pml2[index2], allocate, direct);
    if (pml1 == NULL) {
        return NULL;
    }

    return &pml1[index1];
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Page fault handling
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

err_t virt_handle_page_fault(uintptr_t addr, uint32_t code) {
    err_t err = NO_ERROR;
    virt_lock();

    // ensure its either write protection or non-present, anything
    // else is considered illegal
    CHECK((code & ~(IA32_PF_EC_P | IA32_PF_EC_WR)) == 0);

    void* ptr = (void*)addr;

    // find the root vmar for this request
    vmar_t* root_vmar = NULL;
    if (vmar_contains_ptr(&g_upper_half_vmar, ptr)) {
        root_vmar = &g_upper_half_vmar;
    } else if (vmar_contains_ptr(&g_lower_half_vmar, ptr)) {
        root_vmar = &g_lower_half_vmar;
    } else {
        CHECK_FAIL();
    }

    // Search for the region that we fauled on
    vmar_region_t* region = vmar_find_object(root_vmar, ptr);
    CHECK(region != NULL);

    if (region->object->type == OBJECT_TYPE_VMAR) {
        // we got a VMAR, meaning this is part of
        // the bump allocator
        vmar_t* vmar = containerof(region, vmar_t, region);

        // ensure its inside the bump allocator
        CHECK(vmar->region.start <= ptr && ptr < vmar->bump_alloc_top);

        // ensure its because of an unpresent page
        CHECK(code == IA32_PF_EC_P);

        // allocate a page for it
        void* page = phys_alloc(PAGE_SIZE);
        CHECK_ERROR(page != NULL, ERROR_OUT_OF_MEMORY);
        memset(page, 0, PAGE_SIZE);

        // we are still not mapped, so we didn't
        // race with another core
        // TODO: we might want to turn the bump into something that is more like VMO
        //       in the future for a more common path, and maybe for things like paging
        //       memory
        unlock_direct_map();
        uint64_t* pte = virt_get_pte(ALIGN_DOWN(ptr, PAGE_SIZE), true, false);
        if ((*pte & IA32_PG_P) == 0) {
            *pte = direct_to_phys(page) | IA32_PG_P | IA32_PG_D | IA32_PG_A | IA32_PG_RW | IA32_PG_NX;
        }
        lock_direct_map();

    } else {
        CHECK_FAIL();
    }

    virt_unlock();

cleanup:
    return err;
}
