#include "virt.h"

#include "direct.h"
#include "phys.h"
#include "arch/gdt.h"
#include "arch/intr.h"
#include "arch/paging.h"
#include "lib/ipi.h"
#include "lib/string.h"
#include "sync/spinlock.h"
#include "thread/pcpu.h"

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

/**
 * The tlb entries to flush
 */
static void* m_tlb_flush_addresses[64];

/**
 * A value of zero means to not flush
 * A value of 0-63 means to flush that amount
 * A value of 0xFF means to flush everything
 */
static uint8_t m_tlb_flush_count = 0;

static void tlb_invl_queue(void* addr) {
    // might as well flush it normally on our core
    __invlpg(addr);

    // if no more space just flush everything
    if (m_tlb_flush_count >= ARRAY_LENGTH(m_tlb_flush_addresses)) {
        m_tlb_flush_count = 0xFF;
    }

    // if flushing everything don't add
    if (m_tlb_flush_count == 0xFF) {
        return;
    }

    // add to table and increment the count
    m_tlb_flush_addresses[m_tlb_flush_count++] = addr;
}

static void tlb_invl_commit(void) {
    if (m_tlb_flush_count != 0) {
        ipi_broadcast(IPI_REASON_TLB_FLUSH);
    }
}

static uint64_t* virt_get_next_level(uint64_t* entry, bool allocate, bool kernel) {
    // ensure we don't have a large page in the way
    ASSERT((*entry & IA32_PG_PS) == 0);

    if ((*entry & IA32_PG_P) == 0) {
        if (!allocate) {
            return nullptr;
        }

        void* phys = phys_alloc(PAGE_SIZE);
        if (phys == nullptr) {
            return nullptr;
        }
        memset(phys, 0, PAGE_SIZE);

        *entry = direct_to_phys(phys) | IA32_PG_P | IA32_PG_RW | (kernel ? 0 : IA32_PG_U);
    }

    return phys_to_direct(*entry & PAGING_4K_ADDRESS_MASK);
}

static uint64_t* virt_get_pte(void* virt, bool allocate, bool kernel) {
    size_t index4 = ((uintptr_t)virt >> 39) & PAGING_INDEX_MASK;
    size_t index3 = ((uintptr_t)virt >> 30) & PAGING_INDEX_MASK;
    size_t index2 = ((uintptr_t)virt >> 21) & PAGING_INDEX_MASK;
    size_t index1 = ((uintptr_t)virt >> 12) & PAGING_INDEX_MASK;

    uint64_t* pml3 = virt_get_next_level(&m_pml4[index4], allocate, kernel);
    if (pml3 == nullptr) {
        return nullptr;
    }

    uint64_t* pml2 = virt_get_next_level(&pml3[index3], allocate, kernel);
    if (pml2 == nullptr) {
        return nullptr;
    }

    uint64_t* pml1 = virt_get_next_level(&pml2[index2], allocate, kernel);
    if (pml1 == nullptr) {
        return nullptr;
    }

    return &pml1[index1];
}

void virt_protect(void* virt, size_t page_count, mapping_protection_t protection) {
    for (size_t i = 0; i < page_count; i++, virt += PAGE_SIZE) {
        // get the pte
        uint64_t* pte = virt_get_pte(virt, false, false);
        if (pte == nullptr) {
            continue;
        }

        // change the protections
        uint64_t new_pte = *pte & ~((uint64_t)(IA32_PG_RW | IA32_PG_NX));
        if (protection != MAPPING_PROTECTION_RX) new_pte |= IA32_PG_NX;
        if (protection == MAPPING_PROTECTION_RW) new_pte |= IA32_PG_RW;
        *pte = new_pte;

        tlb_invl_queue(virt);
    }

    tlb_invl_commit();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Page fault handling
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void virt_handle_tlb_flush_ipi(void) {
    ASSERT(m_tlb_flush_count != 0);

    if (m_tlb_flush_count == 0xFF) {
        // flush everything by moving the cr3
        __writecr3(__readcr3());

    } else if (m_tlb_flush_count <= ARRAY_LENGTH(m_tlb_flush_addresses)) {
        // flush the wanted addresses
        for (int i = 0; i < m_tlb_flush_count; i++) {
            __invlpg(m_tlb_flush_addresses[i]);
        }

    } else {
        ASSERT(!"Invalid TLB flush count");
    }
}

err_t virt_handle_page_fault(uintptr_t addr, uint32_t code) {
    err_t err = NO_ERROR;

    vmar_lock();

    // these are the only accesses that could make sense for our handler
    uint32_t allowed_mask = IA32_PF_EC_WRITE | IA32_PF_EC_USER;
    CHECK((code & allowed_mask) == code);

    // get the region it happened in
    vmar_t* mapping = NULL;
    bool kernel = false;
    if (g_kernel_memory.base <= (void*)addr) {
        mapping = &g_kernel_memory;
        CHECK((code & IA32_PF_EC_USER) == 0);
        kernel = true;

    } else if ((void*)addr <= vmar_end(&g_user_memory)) {
        mapping = &g_user_memory;

    } else {
        CHECK_FAIL();
    }

    // search for the actual mapping where we faulted
    mapping = vmar_find_mapping(mapping, (void*)addr);
    CHECK(mapping != NULL);

    // if the type is alloc there are some extra restrictions
    if (mapping->type == VMAR_TYPE_ALLOC) {
        // we don't expect executable pages to ever fault, since at the time
        // we get to them they should be setup properly
        CHECK(mapping->alloc.protection != MAPPING_PROTECTION_RX);

        // if we have a read-only page, don't allow to fault on write
        if (mapping->alloc.protection == MAPPING_PROTECTION_RO) {
            CHECK((code & IA32_PF_EC_WRITE) == 0);
        }
    }

    // get the pte, we assume it was not allocated yet
    // TODO: when we support protection faults we should assume its already allocated
    uint64_t* pte = virt_get_pte((void*)addr, true, mapping == &g_user_memory);
    CHECK(pte != NULL);
    CHECK(*pte == 0);

    // we can now actually do stuff
    uint64_t phys;
    if (mapping->type == VMAR_TYPE_PHYS) {
        // TODO: try to use the largest mapping that fits
        //       or something
        size_t offset = ALIGN_DOWN(addr, PAGE_SIZE) - (uintptr_t)mapping->base;
        phys = mapping->phys.phys + offset;

    } else if (mapping->type == VMAR_TYPE_ALLOC || mapping->type == VMAR_TYPE_STACK) {
        // allocate the page
        void* page = phys_alloc(PAGE_SIZE);
        CHECK(page != NULL);
        memset(page, 0, PAGE_SIZE);
        phys = direct_to_phys(page);

        // TODO: ensure order of stack faults

    } else {
        // invalid type
        CHECK_FAIL();
    }

    // setup the pte, we assume it can't be executable so mark as NX right away
    uint64_t new_pte = phys | IA32_PG_P | IA32_PG_NX | IA32_PG_A | IA32_PG_D;

    // if the mapping is not in kernel then mark as user, otherwise
    // mark as global (assuming we never free kernel addresses)
    if (!kernel)
        new_pte |= IA32_PG_U;
    else
        new_pte |= IA32_PG_G;

    // check if the mapping should be writable
    if (
        (mapping->type == VMAR_TYPE_ALLOC && mapping->alloc.protection == MAPPING_PROTECTION_RW) ||
        mapping->type == VMAR_TYPE_PHYS || mapping->type == VMAR_TYPE_STACK
    ) {
        new_pte |= IA32_PG_RW;
    }

    // TODO: figure caching

    // and set it
    *pte = new_pte;

cleanup:
    if (IS_ERROR(err)) {
        if (g_kernel_memory.base <= (void*)addr) {
            vmar_dump(&g_kernel_memory);
        } else {
            vmar_dump(&g_user_memory);
        }
    }

    vmar_unlock();

    return err;
}
