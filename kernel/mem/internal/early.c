#include "early.h"

#include "direct.h"
#include "limine_requests.h"
#include "phys_map.h"
#include "arch/intrin.h"
#include "arch/paging.h"
#include "lib/elf64.h"
#include "lib/string.h"
#include "mem/vmars.h"

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

#define DIRECT_MAP_ATTRIBUTES   (IA32_PG_P | IA32_PG_D | IA32_PG_A | IA32_PG_RW | IA32_PG_NX | IA32_PG_U)

static uint64_t* early_virt_get_next_level(uint64_t* entry, bool allocate, bool direct) {
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

        *entry = direct_to_phys(phys) | IA32_PG_P | IA32_PG_RW | (direct ? IA32_PG_U : 0);
    }

    return phys_to_direct(*entry & PAGING_4K_ADDRESS_MASK);
}

static uint64_t* early_virt_get_pte(uint64_t* pml4, void* virt, bool allocate, bool direct) {
    size_t index4 = ((uintptr_t)virt >> 39) & PAGING_INDEX_MASK;
    size_t index3 = ((uintptr_t)virt >> 30) & PAGING_INDEX_MASK;
    size_t index2 = ((uintptr_t)virt >> 21) & PAGING_INDEX_MASK;
    size_t index1 = ((uintptr_t)virt >> 12) & PAGING_INDEX_MASK;

    uint64_t* pml3 = early_virt_get_next_level(&pml4[index4], allocate, direct);
    if (pml3 == NULL) {
        return NULL;
    }

    uint64_t* pml2 = early_virt_get_next_level(&pml3[index3], allocate, direct);
    if (pml2 == NULL) {
        return NULL;
    }

    uint64_t* pml1 = early_virt_get_next_level(&pml2[index2], allocate, direct);
    if (pml1 == NULL) {
        return NULL;
    }

    return &pml1[index1];
}

static err_t early_virt_map(
    uint64_t* pml4,
    void* virt, uint64_t phys, size_t num_pages,
    bool write, bool exec, bool direct
) {
    err_t err = NO_ERROR;

    for (size_t i = 0; i < num_pages; i++, virt += PAGE_SIZE, phys += PAGE_SIZE) {
        uint64_t* pte = early_virt_get_pte(pml4, virt, true, direct);
        CHECK_ERROR(pte != NULL, ERROR_OUT_OF_MEMORY);

        // set the entry as requested
        uint64_t entry = phys | IA32_PG_P | IA32_PG_D | IA32_PG_A;
        if (!exec) entry |= IA32_PG_NX;
        if (write) entry |= IA32_PG_RW;
        if (direct) entry |= IA32_PG_U;

        // and set it
        CHECK(*pte == 0);
        *pte = entry;
    }

cleanup:
    return err;
}

static err_t early_virt_unmap_direct(uint64_t* pml4, void* virt) {
    err_t err = NO_ERROR;

    uint64_t* pte = early_virt_get_pte(pml4, virt, false, true);
    CHECK(pte != NULL);
    CHECK(*pte == (direct_to_phys(virt) | DIRECT_MAP_ATTRIBUTES));
    *pte = 0;
    __invlpg(virt);

cleanup:
    return err;
}

//----------------------------------------------------------------------------------------------------------------------
// Initialization of all the mappings
//----------------------------------------------------------------------------------------------------------------------

extern char __kernel_start[];
extern char __kernel_limine_requests_start[];
extern char __kernel_text_start[];
extern char __kernel_rodata_start[];
extern char __kernel_data_start[];
extern char __kernel_end[];

static err_t early_map_kernel(uint64_t* pml4) {
    err_t err = NO_ERROR;

    // The code region is always at -2gb of the upper half
    void* code_region = g_upper_half_vmar.region.end - SIZE_2GB + 1;
    CHECK(code_region >= g_upper_half_vmar.region.start);
    RETHROW(vmar_allocate_static(
        &g_upper_half_vmar,
        &g_code_vmar,
        VMAR_SPECIFIC, code_region - g_upper_half_vmar.region.start,
        SIZE_2GB,
        0
    ));

    // reserve the space for the kernel itself
    CHECK((void*)__kernel_start >= code_region);
    RETHROW(vmar_allocate_static(
        &g_code_vmar,
        &g_kernel_vmar,
        VMAR_SPECIFIC, (void*)__kernel_start - code_region,
        __kernel_end - __kernel_start,
        0
    ));

    // get the physical and virtual base, and ensure that they are the same
    // as what we expect from the kernel start symbol
    CHECK(g_limine_executable_address_request.response != NULL);
    uintptr_t physical_base = g_limine_executable_address_request.response->physical_base;
    uintptr_t virtual_base = g_limine_executable_address_request.response->virtual_base;
    CHECK(virtual_base == (uintptr_t)__kernel_start);

    // and now go over the sections (we just hard code them) and map
    // them properly
    // TODO: something better than this? but I also don't want to look at
    //       the exec file from limine
    struct {
        vmar_t* vmar;
        void* start;
        void* end;
        uint8_t write : 1;
        uint8_t exec: 1;
    } sections[] = {
        { &g_kernel_limine_requests_vmar, __kernel_limine_requests_start, __kernel_text_start, false, false },
        { &g_kernel_text_vmar, __kernel_text_start, __kernel_rodata_start, false, true },
        { &g_kernel_rodata_vmar, __kernel_rodata_start, __kernel_data_start, false, false },
        { &g_kernel_data_vmar, __kernel_data_start, __kernel_end, true, false },
    };
    for (int i = 0; i < ARRAY_LENGTH(sections); i++) {
        void* vaddr = (void*)sections[i].start;
        void* vend = (void*)sections[i].end;

        // reserve in the vmar
        RETHROW(vmar_allocate_static(
            &g_kernel_vmar,
            sections[i].vmar,
            VMAR_SPECIFIC,
            vaddr - (void*)__kernel_start,
            vend - vaddr,
            0
        ));

        uintptr_t paddr = ((uintptr_t)vaddr - virtual_base) + physical_base;
        uintptr_t pend = ((uintptr_t)vend - virtual_base) + physical_base;

        // map it all
        size_t page_num = DIV_ROUND_UP(pend - paddr, PAGE_SIZE);
        RETHROW(early_virt_map(pml4, vaddr, paddr, page_num, sections[i].write, sections[i].exec, false));
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

static err_t early_init_direct_map(void) {
    err_t err = NO_ERROR;

    // get the direct map base if the request was fulfilled
    CHECK(g_limine_hhdm_request.response != NULL);
    void* direct_map_base = (void*)g_limine_hhdm_request.response->offset;

    // the top physical address
    uint64_t top_phys = 1ul << get_physical_address_bits();

    // Setup the vmar of the direct map, this will take into account the KASLR
    // provided by the bootloader
    CHECK(direct_map_base >= g_upper_half_vmar.region.start);
    RETHROW(vmar_allocate_static(
        &g_upper_half_vmar,
        &g_direct_map_vmar,
        VMAR_SPECIFIC, direct_map_base - g_upper_half_vmar.region.start,
        top_phys, 0
    ));

cleanup:
    return err;
}

static err_t early_map_direct_map(uint64_t* pml4) {
    err_t err = NO_ERROR;

    struct limine_memmap_response* response = g_limine_memmap_request.response;
    CHECK(response != NULL);

    // we already reserve everything required by the physical address bits, so
    // no need to check it again
    size_t top_address = 1ULL << get_physical_address_bits();

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
            entry->type == LIMINE_MEMMAP_USABLE
        ) {
            RETHROW(early_virt_map(
                pml4,
                phys_to_direct(entry->base),
                entry->base, entry->length / PAGE_SIZE,
                true, false, true
            ));
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
    RETHROW(vmar_allocate_static(
        &g_upper_half_vmar,
        &g_buddy_bitmap_vmar,
        0, 0,
        total_bitmap_size,
        0
    ));

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
        void* bitmap_ptr = g_buddy_bitmap_vmar.region.start + bitmap_start;
        void* bitmap_end = g_buddy_bitmap_vmar.region.start + bitmap_start + bitmap_size;
        for (; bitmap_ptr < bitmap_end; bitmap_ptr += PAGE_SIZE) {
            uint64_t* pte = early_virt_get_pte(pml4, bitmap_ptr, true, true);
            CHECK_ERROR(pte != NULL, ERROR_OUT_OF_MEMORY);

            // if not allocated already allocate it now
            if ((*pte & IA32_PG_P) == 0) {
                void* page = early_alloc_page();
                CHECK_ERROR(page != NULL, ERROR_OUT_OF_MEMORY);
                memset(page, 0, PAGE_SIZE);

                // unmap it from the direct map, for the most part we don't actually
                // remove things from the direct map, but given the bitmap is static
                // and will never be returned then we can do it
                RETHROW(early_virt_unmap_direct(pml4, page));

                // map the bitmap in the pte
                // we are going to mark this as locked as
                // part of the direct map
                *pte = direct_to_phys(page) | DIRECT_MAP_ATTRIBUTES;
            }
        }
    }

cleanup:
    return err;
}

err_t init_early_mem(void) {
    err_t err = NO_ERROR;

    // start by setting up the direct map
    RETHROW(early_init_direct_map());

    // find the first region for the allocator
    early_alloc_next_region();

    // allocate the pml4
    uint64_t* pml4 = early_alloc_page();
    CHECK_ERROR(pml4 != NULL, ERROR_OUT_OF_MEMORY);

    // map the kernel itself
    RETHROW(early_map_kernel(pml4));
    RETHROW(early_map_direct_map(pml4));
    RETHROW(early_map_buddy_bitmap(pml4));

    // setup the heap region, this is required to continue
    // with setting up other stuff
    RETHROW(vmar_allocate_static(
        &g_upper_half_vmar,
        &g_heap_vmar,
        VMAR_CAN_BUMP,
        0, SIZE_16GB, 0
    ));

    // switch to the page table
    __writecr3(direct_to_phys(pml4));

    vmar_print(&g_upper_half_vmar);
    vmar_print(&g_lower_half_vmar);

cleanup:
    return err;
}

void* early_alloc_get_top(void) {
    return m_early_alloc_top;
}
