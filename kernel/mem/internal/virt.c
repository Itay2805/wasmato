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
// Page table operations
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static err_t virt_map_vmo_page(mapping_t* region, size_t page_index) {
    err_t err = NO_ERROR;

    // get the vmo itself
    CHECK(region->object->type == OBJECT_TYPE_VMO);
    vmo_t* vmo = containerof(region->object, vmo_t, object);

    // we need the offset inside of the vmo
    size_t vmo_page_offset = region->page_offset + page_index;

    // setup the entry for the allocation
    uint64_t entry_template = IA32_PG_P | IA32_PG_D | IA32_PG_A;

    // permissions
    if (region->write) entry_template |= IA32_PG_RW;
    if (!region->exec) entry_template |= IA32_PG_NX;

    // choose the caching
    switch (vmo->cache_policy) {
        case VMO_CACHE_POLICY_CACHED: /* nothing to do */ break;
        case VMO_CACHE_POLICY_UNCACHED: entry_template |= IA32_PG_CACHE_UC; break;
        case VMO_CACHE_POLICY_WRITE_COMBINING: entry_template |= IA32_PG_CACHE_WC_4K; break;
        default: CHECK_FAIL();
    }

    // the physical page
    switch (vmo->type) {
        case VMO_TYPE_NORMAL: {
            // allocate the page if need be
            if ((vmo->pages[vmo_page_offset] & VMO_PAGE_PRESENT) == 0) {
                unlock_direct_map();
                void* ptr = phys_alloc(PAGE_SIZE);
                CHECK_ERROR(ptr != NULL, ERROR_OUT_OF_MEMORY);
                memset(ptr, 0, PAGE_SIZE);
                vmo->pages[vmo_page_offset] = (direct_to_phys(ptr) >> PAGE_SHIFT) | VMO_PAGE_PRESENT;
            }

            entry_template |= vmo->pages[vmo_page_offset] << PAGE_SHIFT;
        } break;

        case VMO_TYPE_PHYSICAL: {
            // for physical this is just the base + the offset
            CHECK(vmo->pages[0] & VMO_PAGE_PRESENT);
            entry_template |= ((vmo->pages[0] + vmo_page_offset) << PAGE_SHIFT);
        } break;

        default:
            CHECK_FAIL();
    }

    // and now we can actually set the page
    unlock_direct_map();
    void* virt = region->start + PAGES_TO_SIZE(page_index);
    uint64_t* pte = virt_get_pte(virt, true);
    CHECK(pte != NULL);
    CHECK(*pte == 0);
    *pte = entry_template;

cleanup:
    lock_direct_map();

    return err;
}

err_t virt_map_and_populate_vmo(mapping_t* region) {
    err_t err = NO_ERROR;

    bool irq_state = irq_spinlock_acquire(&m_virt_lock);

    // just go over all the pages in the region that needs to be
    // mapped and map them right away
    for (size_t i = 0; i < region->page_count; i++) {
        RETHROW(virt_map_vmo_page(region, i));
    }

cleanup:
    irq_spinlock_release(&m_virt_lock, irq_state);

    return err;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Page fault handling
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

err_t virt_handle_page_fault(uintptr_t addr, uint32_t code) {
    err_t err = NO_ERROR;

    bool irq_state = irq_spinlock_acquire(&m_virt_lock);

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
    mapping_t* region = vmar_find_mapping(root_vmar, ptr);
    CHECK(region != NULL);
    size_t page_index = (ptr - region->start) / PAGE_SIZE;
    RETHROW(virt_map_vmo_page(region, page_index));

cleanup:
    irq_spinlock_release(&m_virt_lock, irq_state);

    return err;
}
