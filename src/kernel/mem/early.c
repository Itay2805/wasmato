#include "early.h"

#include <cpuid.h>

#include "direct.h"
#include "limine_requests.h"
#include "phys_map.h"
#include "arch/cpuid.h"
#include "arch/intrin.h"
#include "arch/paging.h"
#include "lib/elf_common.h"
#include "../../runtime/lib/string.h"
#include "mem/mappings.h"

//----------------------------------------------------------------------------------------------------------------------
// Early page allocator
//----------------------------------------------------------------------------------------------------------------------

INIT_DATA static int m_early_alloc_current_index = -1;
INIT_DATA static void* m_early_alloc_top = NULL;

INIT_CODE static void early_alloc_next_region(void) {
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

INIT_CODE static void* early_alloc_page(void) {
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

INIT_CODE static uint64_t* early_virt_get_next_level(uint64_t* entry) {
    if ((*entry & IA32_PG_P) == 0) {
        void* phys = early_alloc_page();
        if (phys == nullptr) {
            return nullptr;
        }
        memset(phys, 0, PAGE_SIZE);

        *entry = direct_to_phys(phys) | IA32_PG_P | IA32_PG_RW;
    }

    return phys_to_direct(*entry & PAGING_4K_ADDRESS_MASK);
}

INIT_CODE static uint64_t* early_virt_get_pte(uint64_t* pml4, void* virt) {
    size_t index4 = ((uintptr_t)virt >> 39) & PAGING_INDEX_MASK;
    size_t index3 = ((uintptr_t)virt >> 30) & PAGING_INDEX_MASK;
    size_t index2 = ((uintptr_t)virt >> 21) & PAGING_INDEX_MASK;
    size_t index1 = ((uintptr_t)virt >> 12) & PAGING_INDEX_MASK;

    uint64_t* pml3 = early_virt_get_next_level(&pml4[index4]);
    if (pml3 == nullptr) {
        return nullptr;
    }

    uint64_t* pml3e = &pml3[index3];
    uint64_t* pml2 = early_virt_get_next_level(pml3e);
    if (pml2 == nullptr) {
        return nullptr;
    }

    uint64_t* pml2e = &pml2[index2];
    uint64_t* pml1 = early_virt_get_next_level(pml2e);
    if (pml1 == nullptr) {
        return nullptr;
    }

    return &pml1[index1];
}

INIT_CODE static bool has_1gb_pages(void) {
    static bool inited = false;
    static bool has_1gb_pages = false;

    if (!inited) {
        // check if we have 1gb pages
        uint32_t a, b, c, d;
        if (__get_cpuid(CPUID_EXTENDED_CPU_SIG, &a, &b, &c, &d)) {
            CPUID_EXTENDED_CPU_SIG_EDX edx = { .raw = d };
            has_1gb_pages = edx.PAGE_1GB;
        }
    }

    return has_1gb_pages;
}

INIT_CODE static bool early_virt_can_map_for_size(void* virt, uintptr_t phys, size_t num_pages, size_t size) {
    return ((uintptr_t)virt % size) == 0 && (phys % size) == 0 && num_pages >= SIZE_TO_PAGES(size);
}

INIT_CODE static err_t early_virt_map(
    uint64_t* pml4,
    void* virt, uint64_t phys, size_t num_pages,
    mapping_protection_t protection
) {
    err_t err = NO_ERROR;

    for (; num_pages != 0; num_pages--, virt += PAGE_SIZE, phys += PAGE_SIZE) {
        uint64_t* pte = early_virt_get_pte(pml4, virt);
        CHECK_ERROR(pte != NULL, ERROR_OUT_OF_MEMORY);

        // set the entry as requested
        uint64_t entry = phys | IA32_PG_P | IA32_PG_A;
        if (protection != MAPPING_PROTECTION_RX) entry |= IA32_PG_NX;
        if (protection == MAPPING_PROTECTION_RW) entry |= IA32_PG_RW | IA32_PG_D;

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

INIT_CODE static err_t early_map_kernel(uint64_t* pml4) {
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
        &g_kernel_init_text_region,
        &g_kernel_init_data_region,
        &g_kernel_text_region,
        &g_kernel_late_rodata_region,
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

INIT_CODE bool early_should_map(struct limine_memmap_entry* entry) {
    return entry->type == LIMINE_MEMMAP_USABLE ||
           entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE;
}

INIT_CODE static uintptr_t early_get_top_address(void) {
    struct limine_memmap_response* response = g_limine_memmap_request.response;
    for (int i = response->entry_count - 1; i >= 0; i--) {
        if (early_should_map(response->entries[i])) {
            return response->entries[i]->base + response->entries[i]->length;
        }
    }
    return 0;
}

INIT_CODE err_t early_init_direct_map(void) {
    err_t err = NO_ERROR;

    vmar_lock();

    // get the direct map base if the request was fulfilled
    CHECK(g_limine_hhdm_request.response != NULL);
    void* direct_map_base = (void*)g_limine_hhdm_request.response->offset;

    // ensure we can have the alignment we need for the direct map
    // to properly use large pages whenever possible
    if (has_1gb_pages()) {
        CHECK(((uintptr_t)direct_map_base % SIZE_1GB) == 0);
    } else {
        CHECK(((uintptr_t)direct_map_base % SIZE_2MB) == 0);
    }

    // Setup the vmar of the direct map, this will take into account the KASLR
    // provided by the bootloader
    g_direct_map_region.base = direct_map_base;
    g_direct_map_region.page_count = SIZE_TO_PAGES(early_get_top_address());
    CHECK_ERROR(vmar_reserve_static(&g_kernel_memory, &g_direct_map_region), ERROR_OUT_OF_MEMORY);

cleanup:
    vmar_unlock();

    return err;
}

INIT_CODE static err_t early_map_direct_map(uint64_t* pml4) {
    err_t err = NO_ERROR;

    struct limine_memmap_response* response = g_limine_memmap_request.response;
    CHECK(response != NULL);

    uintptr_t phys_base = UINTPTR_MAX;
    size_t phys_len = 0;

    // mapp all the usable memory into the direct map
    TRACE("early: Mapping direct map");
    for (int i = 0; i < response->entry_count; i++) {
        struct limine_memmap_entry* entry = response->entries[i];
        if (!early_should_map(entry) || (phys_len != 0 && (phys_base + phys_len) != entry->base)) {
            // got non-mappable entry, map everything
            if (phys_len != 0) {
                TRACE("early: \t%016lx-%016lx", phys_base, phys_base + phys_len - 1);
                RETHROW(early_virt_map(
                    pml4,
                    phys_to_direct(phys_base),
                    phys_base,
                    SIZE_TO_PAGES(phys_len),
                    MAPPING_PROTECTION_RW
                ));
                phys_len = 0;
            }
            continue;
        }

        // ensure everything is aligned properly
        // Limine should handle that
        CHECK((entry->base % PAGE_SIZE) == 0);
        CHECK((entry->length % PAGE_SIZE) == 0);

        // add to the area to map
        if (phys_len == 0) {
            phys_base = entry->base;
        }
        phys_len += entry->length;
    }

    // add the left-overs
    if (phys_len != 0) {
        TRACE("early: \t%016lx-%016lx", phys_base, phys_base + phys_len - 1);
        RETHROW(early_virt_map(
            pml4,
            phys_to_direct(phys_base),
            phys_base,
            SIZE_TO_PAGES(phys_len),
            MAPPING_PROTECTION_RW
        ));
    }

cleanup:
    return err;
}

INIT_CODE static err_t early_map_buddy_bitmap(uint64_t* pml4) {
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
                *pte = direct_to_phys(page) | IA32_PG_P | IA32_PG_D | IA32_PG_A | IA32_PG_RW | IA32_PG_NX | IA32_PG_G;
            }
        }
    }

cleanup:
    return err;
}

INIT_CODE err_t init_early_mem(void) {
    err_t err = NO_ERROR;

    vmar_lock();

    // find the first region for the early allocator
    early_alloc_next_region();

    // allocate the pml4
    uint64_t* pml4 = early_alloc_page();
    CHECK_ERROR(pml4 != NULL, ERROR_OUT_OF_MEMORY);
    memset(pml4, 0, PAGE_SIZE);

    // map the kernel itself
    RETHROW(early_map_kernel(pml4));
    RETHROW(early_map_direct_map(pml4));
    RETHROW(early_map_buddy_bitmap(pml4));

    // switch to the page table
    __writecr3(direct_to_phys(pml4));

cleanup:
    vmar_unlock();

    return err;
}

INIT_CODE void* early_alloc_get_top(void) {
    return m_early_alloc_top;
}
