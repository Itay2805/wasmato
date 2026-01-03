#include "lib/elf64.h"
#include "virt.h"

#include <cpuid.h>
#include <limine_requests.h>
#include <arch/regs.h>

#include "arch/intrin.h"
#include "limine.h"
#include "memory.h"
#include "phys.h"
#include "sync/spinlock.h"
#include "lib/string.h"

#define IA32_PG_P           BIT0
#define IA32_PG_RW          BIT1
#define IA32_PG_U           BIT2
#define IA32_PG_WT          BIT3
#define IA32_PG_CD          BIT4
#define IA32_PG_A           BIT5
#define IA32_PG_D           BIT6
#define IA32_PG_PS          BIT7
#define IA32_PG_PAT_2M      BIT12
#define IA32_PG_PAT_4K      IA32_PG_PS
#define IA32_PG_PMNT        BIT62
#define IA32_PG_NX          BIT63

// These depend on the PAT that is configured by limine
#define IA32_PG_CACHE_WB      (0)
#define IA32_PG_CACHE_WT      (IA32_PG_WT)
#define IA32_PG_CACHE_UCM     (IA32_PG_CD)
#define IA32_PG_CACHE_UC      (IA32_PG_CD | IA32_PG_WT)
#define IA32_PG_CACHE_WP_4K   (IA32_PG_PAT_4K)
#define IA32_PG_CACHE_WC_4K   (IA32_PG_PAT_4K | IA32_PG_WT)
#define IA32_PG_CACHE_WP_2M   (IA32_PG_PAT_2M)
#define IA32_PG_CACHE_WC_2M   (IA32_PG_PAT_2M | IA32_PG_WT)

#define PAGING_4K_ADDRESS_MASK  0x000FFFFFFFFFF000ull
#define PAGING_2M_ADDRESS_MASK  0x000FFFFFFFE00000ull
#define PAGING_1G_ADDRESS_MASK  0x000FFFFFC0000000ull

#define PAGING_4K_MASK  0xFFF
#define PAGING_2M_MASK  0x1FFFFF
#define PAGING_1G_MASK  0x3FFFFFFF

#define PAGING_INDEX_MASK  0x1FF

/**
 * Spinlock for mapping virtual pages
 */
static irq_spinlock_t m_virt_lock = IRQ_SPINLOCK_INIT;

/**
 * The kernel top level cr3
 */
static uint64_t* m_pml4 = 0;

err_t init_virt_early() {
    err_t err = NO_ERROR;

    // get the base and address
    CHECK(g_limine_executable_address_request.response != NULL);
    CHECK(g_limine_executable_address_request.response->virtual_base == 0xffffffff80000000);

    // make sure the HHDM is at the correct address
    CHECK(g_limine_hhdm_request.response != NULL);
    CHECK(g_limine_hhdm_request.response->offset == DIRECT_MAP_OFFSET);

cleanup:
    return err;
}

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

err_t virt_map(void* virt, uint64_t phys, size_t num_pages, map_flags_t flags, map_ops_t* ops) {
    err_t err = NO_ERROR;

    bool irq_state = irq_spinlock_acquire(&m_virt_lock);

    bool need_tlb_shootdown = false;

    for (size_t i = 0; i < num_pages; i++, virt += PAGE_SIZE, phys += PAGE_SIZE) {
        size_t index4 = ((uintptr_t)virt >> 39) & PAGING_INDEX_MASK;
        size_t index3 = ((uintptr_t)virt >> 30) & PAGING_INDEX_MASK;
        size_t index2 = ((uintptr_t)virt >> 21) & PAGING_INDEX_MASK;
        size_t index1 = ((uintptr_t)virt >> 12) & PAGING_INDEX_MASK;

        uint64_t* pml3 = virt_get_next_level(&m_pml4[index4], true);
        CHECK_ERROR(pml3 != NULL, ERROR_OUT_OF_MEMORY);

        uint64_t* pml2 = virt_get_next_level(&pml3[index3], true);
        CHECK_ERROR(pml2 != NULL, ERROR_OUT_OF_MEMORY);

        uint64_t* pml1 = virt_get_next_level(&pml2[index2], true);
        CHECK_ERROR(pml1 != NULL, ERROR_OUT_OF_MEMORY);

        // set the entry as requested
        uint64_t entry = phys | IA32_PG_P | IA32_PG_D | IA32_PG_A;
        if ((flags & MAP_FLAG_EXECUTABLE) == 0) entry |= IA32_PG_NX;
        if ((flags & MAP_FLAG_WRITEABLE) != 0) entry |= IA32_PG_RW;
        if ((flags & MAP_FLAG_UNCACHEABLE) != 0) entry |= IA32_PG_CACHE_UC;

        if (ops != NULL) {
            if (pml1[index1] == entry) {
                // we mapped the same entry again
                if (ops->mapped_same_entry != NULL) {
                    RETHROW(ops->mapped_same_entry(ops, virt, pml1[index1] & PAGING_4K_ADDRESS_MASK));
                }
            } else if (pml1[index1] & IA32_PG_P) {
                // we are mapping with a different entry
                if (ops->mapped_page != NULL) {
                    RETHROW(ops->mapped_page(ops, virt, pml1[index1] & PAGING_4K_ADDRESS_MASK));
                }
            }
        }

        // if we are remapping an existing entry then issue a shootdown
        if ((entry & IA32_PG_P) && pml1[index1] != entry) {
            need_tlb_shootdown = true;
            __invlpg(virt);
        }

        // and set it
        pml1[index1] = entry;
    }

    if (need_tlb_shootdown) {
        // TODO: actually issue the shootdown
    }

cleanup:
    irq_spinlock_release(&m_virt_lock, irq_state);

    return err;
}

err_t virt_unmap(void* virt, size_t num_pages, unmap_ops_t* ops) {
    err_t err = NO_ERROR;

    bool irq_state = irq_spinlock_acquire(&m_virt_lock);

    // TODO: garbage collect table entries
    for (size_t i = 0; i < num_pages; i++, virt += PAGE_SIZE) {
        size_t index4 = ((uintptr_t)virt >> 39) & PAGING_INDEX_MASK;
        size_t index3 = ((uintptr_t)virt >> 30) & PAGING_INDEX_MASK;
        size_t index2 = ((uintptr_t)virt >> 21) & PAGING_INDEX_MASK;
        size_t index1 = ((uintptr_t)virt >> 12) & PAGING_INDEX_MASK;

        // TODO: something more efficient or idk

        uint64_t* pml3 = virt_get_next_level(&m_pml4[index4], false);
        if (pml3 == NULL) {
            if (ops && ops->unmapped_page) {
                RETHROW(ops->unmapped_page(ops, virt));
            }
            continue;
        }

        uint64_t* pml2 = virt_get_next_level(&pml3[index3], false);
        if (pml2 == NULL) {
            if (ops && ops->unmapped_page) {
                RETHROW(ops->unmapped_page(ops, virt));
            }
        }

        uint64_t* pml1 = virt_get_next_level(&pml2[index2], false);
        if (pml1 == NULL) {
            if (ops && ops->unmapped_page) {
                RETHROW(ops->unmapped_page(ops, virt));
            }
        }

        // must already be mapped
        if (ops != NULL) {
            if (pml1[index1] & IA32_PG_P) {
                if (ops->mapped_page) {
                    RETHROW(ops->mapped_page(ops, virt, pml1[index1] & PAGING_4K_ADDRESS_MASK));
                }
            } else {
                if (ops->unmapped_page) {
                    RETHROW(ops->unmapped_page(ops, virt));
                }
            }
        }

        // just remove the entire page entry
        pml1[index1] = 0;
        __invlpg(virt);
    }

    // TODO: queue TLB invalidation on all other cores

cleanup:
    irq_spinlock_release(&m_virt_lock, irq_state);

    return err;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialization
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static err_t virt_map_kernel(void) {
    err_t err = NO_ERROR;

    CHECK(g_limine_executable_file_request.response != NULL);
    void* elf_base = g_limine_executable_file_request.response->executable_file->address;
    Elf64_Ehdr* ehdr = elf_base;
    Elf64_Phdr* phdrs = elf_base + ehdr->e_phoff;

    CHECK(g_limine_executable_address_request.response != NULL);
    uintptr_t physical_base = g_limine_executable_address_request.response->physical_base;
    uintptr_t virtual_base = g_limine_executable_address_request.response->virtual_base;

    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type != PT_LOAD) {
            continue;
        }

        void* vaddr = (void*)phdrs[i].p_vaddr;
        void* vend = (void*)phdrs[i].p_vaddr + phdrs[i].p_memsz;

        uintptr_t paddr = ((uintptr_t)vaddr - virtual_base) + physical_base;
        uintptr_t pend = ((uintptr_t)vend - virtual_base) + physical_base;

        // get the correct flags
        map_flags_t flags = 0;
        if ((phdrs[i].p_flags & PF_W) != 0) flags |= MAP_FLAG_WRITEABLE;
        if ((phdrs[i].p_flags & PF_X) != 0) flags |= MAP_FLAG_EXECUTABLE;

        // map it all
        size_t page_num = DIV_ROUND_UP(pend - paddr, PAGE_SIZE);
        RETHROW(virt_map(vaddr, paddr, page_num, flags, VIRT_MAP_STRICT));
    }

cleanup:
    return err;
}

static err_t virt_map_range(void* ctx, phys_map_type_t type, uint64_t start, size_t length) {
    err_t err = NO_ERROR;

    if (
        type == PHYS_MAP_BOOTLOADER_RECLAIMABLE ||
        type == PHYS_MAP_RAM ||
        type == PHYS_MAP_MMIO_FRAMEBUFFER
    ) {
        RETHROW(virt_map(
            PHYS_TO_DIRECT(start),
            start,
            length / PAGE_SIZE,
            MAP_FLAG_WRITEABLE,
            VIRT_MAP_STRICT)
        );
    }

cleanup:
    return err;
}

err_t init_virt() {
    err_t err = NO_ERROR;

    // setup the top level pml4
    m_pml4 = phys_alloc(PAGE_SIZE);
    CHECK(m_pml4 != NULL);
    memset(m_pml4, 0, PAGE_SIZE);

    // map everything we need
    RETHROW(virt_map_kernel());
    RETHROW(phys_map_iterate(virt_map_range, NULL));

cleanup:
    return err;
}

void switch_page_table() {
    // switch to the page table
    __writecr3(DIRECT_TO_PHYS(m_pml4));

    // enable write protection
    __writecr0(__readcr0() | CR0_WP);
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
    ASSERT(!IS_ERROR(virt_map(PHYS_TO_DIRECT(phys), phys, 1, MAP_FLAG_WRITEABLE, VIRT_MAP_STRICT)));

    // return it to the physical allocator
    phys_free(PHYS_TO_DIRECT(phys), PAGE_SIZE);

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
