#include "virt.h"

#include "direct.h"
#include "mem/vmar.h"
#include "phys.h"
#include "stack.h"
#include "arch/gdt.h"
#include "arch/intr.h"
#include "arch/paging.h"
#include "lib/ipi.h"
#include "sync/spinlock.h"

/**
 * The kernel top level cr3
 */
LATE_RO static uint64_t* m_pml4 = 0;

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

INIT_CODE err_t init_virt(void) {
    // just save the pml4 from the early virt init so we can
    // switch on other cores nicely
    m_pml4 = phys_to_direct(__readcr3() & PAGING_4K_ADDRESS_MASK);

    return NO_ERROR;
}

INIT_CODE void switch_page_table(void) {
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

/**
 * Should we also flush global pages
 */
static bool m_tlb_flush_global = false;

typedef struct invpcid_desc {
    uint64_t pcid : 12;
    uint64_t : 52;
    uint64_t addr;
} PACKED invpcid_desc_t;

void virt_handle_tlb_flush_ipi(void) {
    ASSERT(m_tlb_flush_count != 0);

    if (m_tlb_flush_count == 0xFF) {
        invpcid_desc_t desc = {};

        // flush everything by moving the cr3
        // we use invpcid for those even tho we don't use pcid
        // mostly because it removes the need
        if (m_tlb_flush_global) {
            // flush all contexts with global pages
            // in theory we would use single context with
            // global pages if that existed but no such a
            // thing exists
            _invpcid(3, &desc);
        } else {
            // flush single context without global pages
            _invpcid(1, &desc);
        }

    } else if (m_tlb_flush_count <= ARRAY_LENGTH(m_tlb_flush_addresses)) {
        // flush the wanted addresses
        for (int i = 0; i < m_tlb_flush_count; i++) {
            __invlpg(m_tlb_flush_addresses[i]);
        }

    } else {
        ASSERT(!"Invalid TLB flush count");
    }
}

static void tlb_invl_queue(void* addr, bool is_global) {
    // might as well flush it normally on our core
    __invlpg(addr);

    if (is_global) {
        m_tlb_flush_global = true;
    }

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

        // reset the context
        m_tlb_flush_count = 0;
        m_tlb_flush_global = false;
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

        // TODO: mark as page table

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

void virt_make_global(void* virt) {
    uint64_t* pte = virt_get_pte(virt, false, true);
    ASSERT(pte != nullptr);
    ASSERT((*pte & IA32_PG_G) == 0);
    *pte |= IA32_PG_G;
}

void virt_remove_global(void* virt) {
    vmar_lock();

    uint64_t* pte = virt_get_pte(virt, false, true);
    ASSERT(pte != nullptr);
    ASSERT(*pte & IA32_PG_G);
    *pte &= ~IA32_PG_G;

    // ensure its invalidated
    tlb_invl_queue(virt, true);
    tlb_invl_commit();

    vmar_unlock();
}

static err_t virt_map_direct(void* virt) {
    err_t err = NO_ERROR;

    uint64_t* pte = virt_get_pte(virt, true, true);
    CHECK_ERROR(pte != nullptr, ERROR_OUT_OF_MEMORY);
    CHECK(*pte == 0);

    // map as direct, by default we don't map as global because these pages will get mapped
    // and unmapped and practically we don't access them that much (just when allocating)
    *pte = direct_to_phys(virt) | IA32_PG_P | IA32_PG_D | IA32_PG_A | IA32_PG_RW | IA32_PG_NX;

cleanup:
    return err;
}

static err_t virt_unmap_direct(void* virt) {
    err_t err = NO_ERROR;

    uint64_t* pte = virt_get_pte(virt, false, false);
    CHECK(pte != nullptr);
    CHECK((*pte & IA32_PG_G) == 0);

    CHECK(*pte != 0);
    tlb_invl_queue(virt, false);
    *pte = 0;

    tlb_invl_commit();

cleanup:
    return err;
}

static bool pte_is_present(uint64_t* pte) {
    bool present = *pte & IA32_PG_P;
    if (!present) {
        ASSERT(*pte == 0);
    }
    return present;
}

void virt_protect(void* virt, size_t page_count, mapping_protection_t protection) {
    for (size_t i = 0; i < page_count; i++, virt += PAGE_SIZE) {
        // get the pte
        uint64_t* pte = virt_get_pte(virt, false, false);
        if (pte == nullptr || !pte_is_present(pte)) {
            continue;
        }

        // change the protections
        uint64_t new_pte = *pte & ~((uint64_t)(IA32_PG_RW | IA32_PG_NX | IA32_PG_D));
        if (protection != MAPPING_PROTECTION_RX) new_pte |= IA32_PG_NX;
        if (protection == MAPPING_PROTECTION_RW) new_pte |= IA32_PG_RW | IA32_PG_D;
        *pte = new_pte;

        tlb_invl_queue(virt, *pte & IA32_PG_G);
    }

    tlb_invl_commit();
}

void virt_unmap(void* virt, size_t page_count, bool free) {
    // first mark everything as unmapped so we can properly free it without races
    for (size_t i = 0; i < page_count; i++) {
        void* cur = virt + i * PAGE_SIZE;

        // get the pte
        uint64_t* pte = virt_get_pte(cur, false, false);
        if (pte == nullptr || !pte_is_present(pte)) {
            continue;
        }
        *pte &= ~IA32_PG_P;
        tlb_invl_queue(cur, *pte & IA32_PG_G);
    }

    // actually commit to all the cores that we are now unmapped
    tlb_invl_commit();

    // now that all the cores see it as unmapped, we are going to
    // actually unmap everything
    for (size_t i = 0; i < page_count; i++) {
        // get the pte
        uint64_t* pte = virt_get_pte(virt + i * PAGE_SIZE, false, false);
        if (pte == nullptr || *pte == 0) {
            continue;
        }

        // free the page if we should
        if (free) {
            void* ptr = phys_to_direct(*pte & PAGING_4K_ADDRESS_MASK);
            ASSERT(!IS_ERROR(virt_map_direct(ptr)));
            phys_free(ptr, PAGE_SIZE);
        }

        // clear the entire pte
        *pte = 0;
    }
}

err_t virt_setup_shadow_stack(void* virt) {
    err_t err = NO_ERROR;

    // allocate the page
    uint64_t* page = phys_alloc(PAGE_SIZE);
    CHECK_ERROR(page != nullptr, ERROR_OUT_OF_MEMORY);

    // setup the supervisor token
    *page = (uintptr_t)page;

    // remove it from the direct map
    virt_unmap_direct(page);

    // map the entry correctly, needs to be a readonly
    // with dirty bit set page
    uint64_t* pte = virt_get_pte(virt, true, true);
    CHECK_ERROR(pte != nullptr, ERROR_OUT_OF_MEMORY);
    CHECK(*pte == 0);
    *pte = direct_to_phys(page) | IA32_PG_P | IA32_PG_NX | IA32_PG_A | IA32_PG_D;

cleanup:
    return err;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Init memory reclamation
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

INIT_CODE void protect_ro_data(void) {
    vmar_lock();

    TRACE("virt: reprotecting late rodata");
    g_kernel_late_rodata_region.alloc.protection = MAPPING_PROTECTION_RO;
    virt_protect(g_kernel_late_rodata_region.base, g_kernel_late_rodata_region.page_count, MAPPING_PROTECTION_RO);

    vmar_unlock();
}

void reclaim_init_mem(void) {
    vmar_lock();

    TRACE("virt: Reclaiming init code/data");

    // NOTE: we explicitly don't remove the vmars because we are going to use them
    //       to ensure nothing ever maps back, so even if we accidently call init
    //       code it will never map to anything valid and will fault instead

    vmar_t* vmar[] = {
        &g_kernel_limine_requests_region,
        &g_kernel_init_text_region,
        &g_kernel_init_data_region,
    };
    for (int i = 0; i < ARRAY_LENGTH(vmar); i++) {
        TRACE("virt: \t%p-%p", vmar[i]->base, vmar[i]->base + PAGES_TO_SIZE(vmar[i]->page_count) - 1);

        for (size_t j = 0; j < vmar[i]->page_count; j++) {
            void* virt = vmar[i]->base + j * PAGE_SIZE;

            uint64_t* pte = virt_get_pte(virt, false, true);
            ASSERT(pte != nullptr);

            uint64_t phys = *pte & PAGING_4K_ADDRESS_MASK;
            tlb_invl_queue(virt, *pte & IA32_PG_G);
            *pte = 0;

            // Return physical page to buddy allocator
            void* ptr = phys_to_direct(phys);
            virt_map_direct(ptr);
            phys_free(ptr, PAGE_SIZE);
        }
    }

    tlb_invl_commit();

    vmar_unlock();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Page fault handling
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

err_t virt_handle_page_fault(uintptr_t addr, uint32_t code) {
    err_t err = NO_ERROR;

    vmar_lock();

    // these are the only accesses that could make sense for our handler
    uint32_t allowed_mask = IA32_PF_EC_WRITE | IA32_PF_EC_USER | IA32_PF_EC_SHSTK;
    CHECK((code & allowed_mask) == code);

    // get the region it happened in
    vmar_t* mapping = nullptr;
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

    if (mapping->type == VMAR_TYPE_SHADOW_STACK) {
        // ensure we actually enabled shadow stack support
        CHECK(g_shadow_stack_supported);

        // fault on shadow stack must come from the shadow stack
        // access itself and not from a normal access
        CHECK((code & IA32_PF_EC_SHSTK) != 0);
    } else {
        // any other region should never have a shadow stack access
        CHECK((code & IA32_PF_EC_SHSTK) == 0);
    }

    // get the pte, we assume it was not allocated yet
    // TODO: when we support protection faults we should assume its already allocated
    uint64_t* pte = virt_get_pte((void*)addr, true, mapping == &g_user_memory);
    CHECK(pte != NULL);

    // handle race between page faults, if the error is page not found, and the
    // pte has the present flag, assume it was set already by another core
    if ((code & IA32_PF_EC_PROT) == 0) {
        if ((*pte & IA32_PG_P) != 0) {
            goto cleanup;
        } else {
            // ensure we have an empty pte and nothing weird
            CHECK(*pte == 0, "%lx", *pte);
        }
    } else {
        // ensure the page actually exists, idk what to do
        // if it does not exist
        CHECK(*pte & IA32_PG_P);
    }

    // we can now actually do stuff
    uint64_t phys;
    if (mapping->type == VMAR_TYPE_PHYS) {
        // TODO: try to use the largest mapping that fits
        //       or something
        size_t offset = ALIGN_DOWN(addr, PAGE_SIZE) - (uintptr_t)mapping->base;
        phys = mapping->phys.phys + offset;

    } else if (mapping->type == VMAR_TYPE_ALLOC || mapping->type == VMAR_TYPE_STACK || mapping->type == VMAR_TYPE_SHADOW_STACK) {
        // allocate the page
        void* page = phys_alloc(PAGE_SIZE);
        CHECK(page != NULL);
        memset(page, 0, PAGE_SIZE);
        RETHROW(virt_unmap_direct(page));
        phys = direct_to_phys(page);

        // TODO: ensure order of stack faults

    } else {
        // invalid type
        CHECK_FAIL();
    }

    // setup the pte, we assume it can't be executable so mark as NX right away
    uint64_t new_pte = phys | IA32_PG_P | IA32_PG_NX | IA32_PG_A;

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
        // we don't need the dirty bit, so set it right away, we don't
        // always set it because for shadow stacks we need it to be on
        // on a non-writable page
        new_pte |= IA32_PG_RW | IA32_PG_D;
    } else if (mapping->type == VMAR_TYPE_SHADOW_STACK) {
        // if this is a shadow stack then mark it with the dirty bit,
        // which is basically how you mark a page to be usable as shadow
        // stack
        new_pte |= IA32_PG_D;
    }

    // TODO: figure caching

    // and set it
    *pte = new_pte;

cleanup:
    if (IS_ERROR(err)) {
        if (addr > SIZE_4KB) {
            vmar_dump(&g_user_memory);
            vmar_dump(&g_kernel_memory);
        }
    } else {
        // don't unlock if we got an error
        // so we can panic properly
        vmar_unlock();
    }


    return err;
}
