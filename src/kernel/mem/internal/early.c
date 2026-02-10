#include "early.h"

#include <cpuid.h>

#include "direct.h"
#include "limine_requests.h"
#include "phys_map.h"
#include "arch/cpuid.h"
#include "arch/intrin.h"
#include "arch/paging.h"
#include "lib/elf_common.h"
#include "lib/string.h"
#include "mem/mappings.h"

//----------------------------------------------------------------------------------------------------------------------
// Early page allocator
//----------------------------------------------------------------------------------------------------------------------

static int m_early_alloc_current_index = -1;
static void* m_early_alloc_top = NULL;

static void early_alloc_next_region(void) {
    // go over an entry to skip the current one, we start
    // as -1 which is going to start from zero
    m_early_alloc_current_index++;

    // search for a usable range
    struct limine_memmap_response* response = g_limine_memmap_request.response;
    for (; m_early_alloc_current_index < response->entry_count; m_early_alloc_current_index++) {
        if (response->entries[m_early_alloc_current_index]->type == LIMINE_MEMMAP_USABLE) {
            break;
        }
    }

    // if we couldn't find anything then set the index to -1 so we know we are done
    if (m_early_alloc_current_index >= response->entry_count) {
        m_early_alloc_current_index = -1;
        return;
    }

    // find the current top
    m_early_alloc_top = phys_to_direct(response->entries[m_early_alloc_current_index]->base);
}

static void* early_alloc_page(void) {
    if (m_early_alloc_current_index < 0) {
        // no more pages to give out
        return NULL;
    }

    // alloc the page
    void* ptr = m_early_alloc_top;
    m_early_alloc_top += PAGE_SIZE;

    // check if we have finished the range, if so advance to the next range
    // for more pages to allocate from
    struct limine_memmap_response* response = g_limine_memmap_request.response;
    void* region_top = phys_to_direct(response->entries[m_early_alloc_current_index]->base) + response->entries[m_early_alloc_current_index]->length;
    if (m_early_alloc_top >= region_top) {
        early_alloc_next_region();
    }

    return ptr;
}

//----------------------------------------------------------------------------------------------------------------------
// Early mapping utilities
//----------------------------------------------------------------------------------------------------------------------

/**
 * The bits to set to all kernel entries (except the kernel itself)
 * basically does RW and global
 */
#define KERNEL_PTE_BITS (IA32_PG_P | IA32_PG_D | IA32_PG_A | IA32_PG_RW | IA32_PG_NX | IA32_PG_PS | IA32_PG_G)

static uint64_t* early_virt_get_next_level(uint64_t* entry) {
    // ensure we don't have a large page in the way
    ASSERT((*entry & IA32_PG_PS) == 0);

    if ((*entry & IA32_PG_P) == 0) {
        void* phys = early_alloc_page();
        if (phys == NULL) {
            return NULL;
        }
        memset(phys, 0, PAGE_SIZE);

        *entry = direct_to_phys(phys) | IA32_PG_P | IA32_PG_RW;
    }

    return phys_to_direct(*entry & PAGING_4K_ADDRESS_MASK);
}

static uint64_t* early_virt_get_pte(uint64_t* pml4, void* virt) {
    size_t index4 = ((uintptr_t)virt >> 39) & PAGING_INDEX_MASK;
    size_t index3 = ((uintptr_t)virt >> 30) & PAGING_INDEX_MASK;
    size_t index2 = ((uintptr_t)virt >> 21) & PAGING_INDEX_MASK;
    size_t index1 = ((uintptr_t)virt >> 12) & PAGING_INDEX_MASK;

    uint64_t* pml3 = early_virt_get_next_level(&pml4[index4]);
    if (pml3 == NULL) {
        return NULL;
    }

    uint64_t* pml2 = early_virt_get_next_level(&pml3[index3]);
    if (pml2 == NULL) {
        return NULL;
    }

    uint64_t* pml1 = early_virt_get_next_level(&pml2[index2]);
    if (pml1 == NULL) {
        return NULL;
    }

    return &pml1[index1];
}

static err_t early_virt_map(
    uint64_t* pml4,
    void* virt, uint64_t phys, size_t num_pages,
    mapping_protection_t protection
) {
    err_t err = NO_ERROR;

    for (size_t i = 0; i < num_pages; i++, virt += PAGE_SIZE, phys += PAGE_SIZE) {
        uint64_t* pte = early_virt_get_pte(pml4, virt);
        CHECK_ERROR(pte != NULL, ERROR_OUT_OF_MEMORY);

        // set the entry as requested
        uint64_t entry = phys | IA32_PG_P | IA32_PG_D | IA32_PG_A | IA32_PG_G;
        if (protection != MAPPING_PROTECTION_RX) entry |= IA32_PG_NX;
        if (protection == MAPPING_PROTECTION_RW) entry |= IA32_PG_RW;

        // and set it
        CHECK(*pte == 0);
        *pte = entry;
    }

cleanup:
    return err;
}

//----------------------------------------------------------------------------------------------------------------------
// Initialization of all the mappings
//----------------------------------------------------------------------------------------------------------------------

static err_t early_map_kernel(uint64_t* pml4) {
    err_t err = NO_ERROR;

    // The kernel region is at the -2gb, its used only for kernel stuff
    CHECK_ERROR(vmar_reserve_static(&g_kernel_memory, &g_kernel_region), ERROR_OUT_OF_MEMORY);

    // get the physical and virtual base, and ensure that they are the same
    // as what we expect from the kernel start symbol
    CHECK(g_limine_executable_address_request.response != NULL);
    uint64_t physical_base = g_limine_executable_address_request.response->physical_base;
    void* virtual_base = (void*)g_limine_executable_address_request.response->virtual_base;

    vmar_t* kernel_regions[] = {
        &g_kernel_limine_requests_region,
        &g_kernel_text_region,
        &g_kernel_rodata_region,
        &g_kernel_data_region,
    };

    for (int i = 0; i < ARRAY_LENGTH(kernel_regions); i++) {
        vmar_t* vmar = kernel_regions[i];
        uint64_t phys_base = (vmar->base - virtual_base) + physical_base;
        CHECK(vmar_reserve_static(&g_kernel_region, vmar));
        RETHROW(early_virt_map(
            pml4, vmar->base, phys_base,
            vmar->page_count,
            vmar->alloc.protection
        ));
    }

    g_kernel_region.locked = true;

cleanup:
    return err;
}

static err_t early_init_direct_map(void) {
    err_t err = NO_ERROR;

    // get the direct map base if the request was fulfilled
    CHECK(g_limine_hhdm_request.response != NULL);
    void* direct_map_base = (void*)g_limine_hhdm_request.response->offset;
    CHECK(((uintptr_t)direct_map_base % SIZE_1GB) == 0);

    // the top physical address
    uint64_t top_phys = 1ul << get_physical_address_bits();

    // Setup the vmar of the direct map, this will take into account the KASLR
    // provided by the bootloader
    g_direct_map_region.base = direct_map_base;
    g_direct_map_region.page_count = SIZE_TO_PAGES(top_phys);
    CHECK_ERROR(vmar_reserve_static(&g_kernel_memory, &g_direct_map_region), ERROR_OUT_OF_MEMORY);

cleanup:
    return err;
}

static err_t early_map_direct_map(uint64_t* pml4) {
    err_t err = NO_ERROR;

    struct limine_memmap_response* response = g_limine_memmap_request.response;
    CHECK(response != NULL);

    // check if we have 1gb pages
    bool has_1gb_pages = false;
    uint32_t a, b, c, d;
    if (__get_cpuid(CPUID_EXTENDED_CPU_SIG, &a, &b, &c, &d)) {
        CPUID_EXTENDED_CPU_SIG_EDX edx = { .packed = d };
        has_1gb_pages = edx.Page1GB;
    }

    // we already reserve everything required by the physical address bits, so
    // no need to check it again
    size_t top_address = 1ULL << get_physical_address_bits();

    // the amount of top level entries we need, each entry is a 512gb range
    // we can assume the value is correct because by this time the direct map
    // was reserved in the VMAR
    size_t pml4e_count = DIV_ROUND_UP(top_address, SIZE_512GB);

    // the amount of entries inside hte pml4e, because the phys bits are a log2
    // we will only ever have a value that is less than 512 if there is less than
    // 512gb of physical address space
    size_t pml3e_count = MIN(DIV_ROUND_UP(top_address, SIZE_1GB), 512);

    // map it all
    for (size_t pml4i = 0; pml4i < pml4e_count; pml4i++) {
        uint64_t* pml3 = early_alloc_page();
        CHECK_ERROR(pml3 != NULL, ERROR_OUT_OF_MEMORY);
        memset(pml3, 0, PAGE_SIZE);

        // setup the pml4 entry, we offset by 256
        // because upper half
        pml4[256 + pml4i] = direct_to_phys(pml3) | IA32_PG_P | IA32_PG_RW | IA32_PG_NX | IA32_PG_U;

        // and now the rest of the entries
        for (size_t pml3i = 0; pml3i < pml3e_count; pml3i++) {
            // and just map it as 1gb pages
            CHECK(has_1gb_pages);
            pml3[pml3i] = ((SIZE_512GB * pml4i) + (SIZE_1GB * pml3i)) | KERNEL_PTE_BITS | IA32_PG_PS;
        }
    }

cleanup:
    return err;
}

static err_t early_map_buddy_bitmap(uint64_t* pml4) {
    err_t err = NO_ERROR;

    struct limine_memmap_response* response = g_limine_memmap_request.response;
    CHECK(response != NULL);

    // reserve space for the bitmap itself, we need to ensure we
    // can fit the entire physical address space in it
    uint64_t top_address = 1ULL << get_physical_address_bits();
    size_t total_bitmap_size = ALIGN_UP(DIV_ROUND_UP(DIV_ROUND_UP(top_address, PAGE_SIZE), 8), PAGE_SIZE);
    g_buddy_bitmap_region.page_count = SIZE_TO_PAGES(total_bitmap_size);
    CHECK_ERROR(vmar_reserve_static(&g_kernel_memory, &g_buddy_bitmap_region), ERROR_OUT_OF_MEMORY);

    // map all the ranges now
    for (int i = 0; i < response->entry_count; i++) {
        struct limine_memmap_entry* entry = response->entries[i];
        if (entry->type != LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE && entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        // calculate the bitmap range that we need to allocate
        size_t bitmap_start = ALIGN_DOWN((entry->base / PAGE_SIZE) / 8, PAGE_SIZE);
        size_t bitmap_size = ALIGN_UP(DIV_ROUND_UP(entry->length / PAGE_SIZE, 8), PAGE_SIZE);

        // map the entire bitmap right now
        void* bitmap_ptr = g_buddy_bitmap_region.base + bitmap_start;
        void* bitmap_end = g_buddy_bitmap_region.base + bitmap_start + bitmap_size;
        for (; bitmap_ptr < bitmap_end; bitmap_ptr += PAGE_SIZE) {
            uint64_t* pte = early_virt_get_pte(pml4, bitmap_ptr);
            CHECK_ERROR(pte != NULL, ERROR_OUT_OF_MEMORY);

            // if not allocated already allocate it now
            if ((*pte & IA32_PG_P) == 0) {
                void* page = early_alloc_page();
                CHECK_ERROR(page != NULL, ERROR_OUT_OF_MEMORY);
                memset(page, 0, PAGE_SIZE);

                // map the bitmap in the pte
                // we are going to mark this as locked as
                // part of the direct map
                *pte = direct_to_phys(page) | KERNEL_PTE_BITS;
            }
        }
    }

cleanup:
    return err;
}

err_t init_early_mem(void) {
    err_t err = NO_ERROR;

    // start by setting up the direct map, this is needed to make
    // sure we can virt-to-phys and phys-to-virt
    RETHROW(early_init_direct_map());

    // find the first region for the early allocator
    early_alloc_next_region();

    // allocate the pml4
    uint64_t* pml4 = early_alloc_page();
    CHECK_ERROR(pml4 != NULL, ERROR_OUT_OF_MEMORY);

    // map the kernel itself
    RETHROW(early_map_kernel(pml4));
    RETHROW(early_map_direct_map(pml4));
    RETHROW(early_map_buddy_bitmap(pml4));

    // switch to the page table
    __writecr3(direct_to_phys(pml4));

cleanup:
    return err;
}

void* early_alloc_get_top(void) {
    return m_early_alloc_top;
}
