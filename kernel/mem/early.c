#include "early.h"

#include <cpuid.h>
#include <stdbool.h>
#include <stddef.h>

#include "limine.h"
#include "limine_requests.h"
#include "phys_map.h"
#include "memory.h"
#include "phys.h"
#include "virt.h"
#include "arch/cpuid.h"
#include "arch/intrin.h"
#include "arch/regs.h"
#include "lib/elf64.h"
#include "lib/except.h"
#include "lib/string.h"

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
    m_early_alloc_top = PHYS_TO_DIRECT(response->entries[m_early_alloc_current_index]->base);
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
    void* region_top = PHYS_TO_DIRECT(response->entries[m_early_alloc_current_index]->base) + response->entries[m_early_alloc_current_index]->length;
    if (m_early_alloc_top >= region_top) {
        early_alloc_next_region();
    }

    return ptr;
}

//----------------------------------------------------------------------------------------------------------------------
// Early mapping utilities
//----------------------------------------------------------------------------------------------------------------------

#define DIRECT_MAP_ATTRIBUTES   (IA32_PG_P | IA32_PG_D | IA32_PG_A | IA32_PG_RW | IA32_PG_NX)

static uint64_t* early_virt_get_next_level(uint64_t* entry, bool allocate) {
    // ensure we don't have a large page in the way
    ASSERT((*entry & IA32_PG_PS) == 0);

    if ((*entry & IA32_PG_P) == 0) {
        if (!allocate) {
            return NULL;
        }

        void* phys = early_alloc_page();
        if (phys == NULL) {
            return NULL;
        }
        memset(phys, 0, PAGE_SIZE);

        *entry = DIRECT_TO_PHYS(phys) | IA32_PG_P | IA32_PG_RW;
    }

    return PHYS_TO_DIRECT(*entry & PAGING_4K_ADDRESS_MASK);
}

static uint64_t* early_virt_get_pte(uint64_t* pml4, void* virt, bool allocate) {
    size_t index4 = ((uintptr_t)virt >> 39) & PAGING_INDEX_MASK;
    size_t index3 = ((uintptr_t)virt >> 30) & PAGING_INDEX_MASK;
    size_t index2 = ((uintptr_t)virt >> 21) & PAGING_INDEX_MASK;
    size_t index1 = ((uintptr_t)virt >> 12) & PAGING_INDEX_MASK;

    uint64_t* pml3 = early_virt_get_next_level(&pml4[index4], allocate);
    if (pml3 == NULL) {
        return NULL;
    }

    uint64_t* pml2 = early_virt_get_next_level(&pml3[index3], allocate);
    if (pml2 == NULL) {
        return NULL;
    }

    uint64_t* pml1 = early_virt_get_next_level(&pml2[index2], allocate);
    if (pml1 == NULL) {
        return NULL;
    }

    return &pml1[index1];
}

static err_t early_virt_map(uint64_t* pml4, void* virt, uint64_t phys, size_t num_pages, map_flags_t flags) {
    err_t err = NO_ERROR;

    for (size_t i = 0; i < num_pages; i++, virt += PAGE_SIZE, phys += PAGE_SIZE) {
        uint64_t* pte = early_virt_get_pte(pml4, virt, true);
        CHECK_ERROR(pte != NULL, ERROR_OUT_OF_MEMORY);

        // set the entry as requested
        uint64_t entry = phys | IA32_PG_P | IA32_PG_D | IA32_PG_A;
        if ((flags & MAP_FLAG_EXECUTABLE) == 0) entry |= IA32_PG_NX;
        if ((flags & MAP_FLAG_WRITEABLE) != 0) entry |= IA32_PG_RW;
        if ((flags & MAP_FLAG_UNCACHEABLE) != 0) entry |= IA32_PG_CACHE_UC;

        // and set it
        CHECK(*pte == 0);
        *pte = entry;
    }

cleanup:
    return err;
}

static err_t early_virt_unmap_direct(uint64_t* pml4, void* virt) {
    err_t err = NO_ERROR;

    uint64_t* pte = early_virt_get_pte(pml4, virt, false);
    CHECK(pte != NULL);
    CHECK(*pte == (DIRECT_TO_PHYS(virt) | DIRECT_MAP_ATTRIBUTES));
    *pte = 0;
    __invlpg(virt);

cleanup:
    return err;
}

//----------------------------------------------------------------------------------------------------------------------
// Initialization of all the mappings
//----------------------------------------------------------------------------------------------------------------------

static err_t early_virt_map_kernel(uint64_t* pml4) {
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
        RETHROW(early_virt_map(pml4, vaddr, paddr, page_num, flags));
    }

cleanup:
    return err;
}

static const char* m_limine_type_str[] = {
    [LIMINE_MEMMAP_USABLE] = "Usable",
    [LIMINE_MEMMAP_RESERVED] = "Reserved",
    [LIMINE_MEMMAP_ACPI_RECLAIMABLE] = "ACPI Reclaimable",
    [LIMINE_MEMMAP_ACPI_NVS] = "ACPI NVS",
    [LIMINE_MEMMAP_BAD_MEMORY] = "Bad memory",
    [LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE] = "Bootloader Reclaimable",
    [LIMINE_MEMMAP_EXECUTABLE_AND_MODULES] = "Kernel and modules",
    [LIMINE_MEMMAP_FRAMEBUFFER] = "Framebuffer",
    [LIMINE_MEMMAP_ACPI_TABLES] = "ACPI Tables",
};

static err_t early_virt_map_direct(uint64_t* pml4) {
    err_t err = NO_ERROR;

    struct limine_memmap_response* response = g_limine_memmap_request.response;
    CHECK(response != NULL);

    // ensure the direct map can hold that amount of physical memory
    uintptr_t phys_addr_bits = get_physical_address_bits();
    size_t top_address = 1ULL << phys_addr_bits;
    CHECK(top_address <= DIRECT_MAP_SIZE);

    // map all the physical memory we need to access
    TRACE("memory: Bootloader provided memory map:");
    for (int i = 0; i < response->entry_count; i++) {
        struct limine_memmap_entry* entry = response->entries[i];
        if (entry->type < ARRAY_LENGTH(m_limine_type_str) && m_limine_type_str[entry->type] != NULL) {
            TRACE("memory: \t%016lx-%016lx: %s", entry->base, entry->base + entry->length, m_limine_type_str[entry->type]);
        } else {
            TRACE("memory: \t%016lx-%016lx: <unknown type %lu>", entry->base, entry->base + entry->length, entry->type);
        }

        // must be below the address limit
        CHECK(entry->base + entry->length <= top_address);

        if (
            entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
            entry->type == LIMINE_MEMMAP_USABLE ||
            entry->type == LIMINE_MEMMAP_FRAMEBUFFER
        ) {
            RETHROW(early_virt_map(
                pml4,
                PHYS_TO_DIRECT(entry->base),
                entry->base, entry->length / PAGE_SIZE,
                MAP_FLAG_WRITEABLE
            ));
        }
    }

cleanup:
    return err;
}

static err_t early_virt_map_buddy_bitmap(uint64_t* pml4) {
    err_t err = NO_ERROR;

    struct limine_memmap_response* response = g_limine_memmap_request.response;
    CHECK(response != NULL);

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
        void* bitmap_ptr = PHYS_BUDDY_BITMAP_START + bitmap_start;
        void* bitmap_end = PHYS_BUDDY_BITMAP_START + bitmap_start + bitmap_size;
        for (; bitmap_ptr < bitmap_end; bitmap_ptr += PAGE_SIZE) {
            uint64_t* pte = early_virt_get_pte(pml4, bitmap_ptr, true);
            CHECK_ERROR(pte != NULL, ERROR_OUT_OF_MEMORY);

            // if not allocated already allocate it now
            if ((*pte & IA32_PG_P) == 0) {
                void* page = early_alloc_page();
                CHECK_ERROR(page != NULL, ERROR_OUT_OF_MEMORY);
                memset(page, 0, PAGE_SIZE);

                // unmap it from the direct map
                early_virt_unmap_direct(pml4, page);

                // map the bitmap in the pte
                *pte = DIRECT_TO_PHYS(page) | DIRECT_MAP_ATTRIBUTES;
            }
        }
    }

cleanup:
    return err;
}

err_t init_virt_early(void) {
    err_t err = NO_ERROR;

    // find the first region for the allocator
    early_alloc_next_region();

    // allocate the pml4
    uint64_t* pml4 = early_alloc_page();
    CHECK_ERROR(pml4 != NULL, ERROR_OUT_OF_MEMORY);

    // map the kernel itself
    RETHROW(early_virt_map_kernel(pml4));
    RETHROW(early_virt_map_direct(pml4));
    RETHROW(early_virt_map_buddy_bitmap(pml4));

    // switch to the page table
    __writecr3(DIRECT_TO_PHYS(pml4));

    // enable write protection
    __writecr0(__readcr0() | CR0_WP);

cleanup:
    return err;
}

void* early_alloc_get_top(void) {
    return m_early_alloc_top;
}
