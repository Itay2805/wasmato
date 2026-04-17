#include "phys_map.h"

#include <cpuid.h>

#include "limine.h"
#include "limine_requests.h"
#include "alloc.h"
#include "arch/cpuid.h"


rb_root_t g_phys_map = RB_ROOT;
spinlock_t g_phys_map_lock = SPINLOCK_INIT;

/**
 * Allocator for phys map entries
 */
static mem_alloc_t m_phys_map_alloc;

static __always_inline bool phys_map_less(struct rb_node* a, const struct rb_node* b) {
    const phys_map_entry_t* ea = rb_entry(a, phys_map_entry_t, node);
    const phys_map_entry_t* eb = rb_entry(b, phys_map_entry_t, node);
    return ea->start < eb->start;
}

// rb_find comparator: locate the entry whose [start, end] contains `*key`
static int phys_map_cmp_contains(const void* key, const struct rb_node* n) {
    uint64_t addr = *(const uint64_t*)key;
    const phys_map_entry_t* entry = rb_entry(n, phys_map_entry_t, node);
    if (addr < entry->start) return -1;
    if (addr > entry->end)   return 1;
    return 0;
}

static phys_map_entry_t* phys_map_insert_new_entry(uint64_t start, uint64_t end, phys_map_type_t type) {
    phys_map_entry_t* entry = mem_calloc(&m_phys_map_alloc);
    ASSERT(entry != NULL);

    entry->type = type;
    entry->start = start;
    entry->end = end;
    rb_add(&entry->node, &g_phys_map, phys_map_less);
    return entry;
}

static void phys_map_remove_old_entry(phys_map_entry_t* entry) {
    rb_erase(&entry->node, &g_phys_map);
    mem_free(&m_phys_map_alloc, entry);
}

void phys_map_convert_locked(phys_map_type_t type, uint64_t start, size_t length) {
    uint64_t end = start + length - 1;

    // Locate the existing entry that covers `start` (O(log n)).
    //
    // After the initial full-range insert, the tree always covers the entire
    // physical address space, so every subsequent convert falls into this
    // branch with a single containing entry. The NULL case only fires for the
    // very first call (empty tree) or for ranges outside the currently-mapped
    // span (which the caller is responsible for keeping contiguous).
    rb_node_t* found = rb_find(&start, &g_phys_map, phys_map_cmp_contains);
    phys_map_entry_t* entry;

    if (found == NULL) {
        entry = phys_map_insert_new_entry(start, end, type);
    } else {
        entry = rb_entry(found, phys_map_entry_t, node);
        ASSERT(entry->end >= end, "phys_map: partial overlap not supported");

        if (entry->type == type) {
            return;
        }

        // Shrink `entry` down to the new range FIRST so the split pieces
        // we insert below don't momentarily share the same `start` key.
        uint64_t orig_start = entry->start;
        uint64_t orig_end = entry->end;
        phys_map_type_t orig_type = entry->type;
        entry->start = start;
        entry->end = end;
        entry->type = type;

        if (orig_start < start) {
            phys_map_insert_new_entry(orig_start, start - 1, orig_type);
        }
        if (orig_end > end) {
            phys_map_insert_new_entry(end + 1, orig_end, orig_type);
        }
    }

    // Coalesce with same-type, contiguous neighbours. Check prev first (may
    // replace `entry`), then check the survivor's next.
    rb_node_t* prev_node = rb_prev(&entry->node);
    if (prev_node != NULL) {
        phys_map_entry_t* prev = rb_entry(prev_node, phys_map_entry_t, node);
        if (prev->type == entry->type && prev->end + 1 == entry->start) {
            prev->end = entry->end;
            phys_map_remove_old_entry(entry);
            entry = prev;
        }
    }

    rb_node_t* next_node = rb_next(&entry->node);
    if (next_node != NULL) {
        phys_map_entry_t* next = rb_entry(next_node, phys_map_entry_t, node);
        if (entry->type == next->type && entry->end + 1 == next->start) {
            entry->end = next->end;
            phys_map_remove_old_entry(next);
        }
    }
}

void phys_map_convert(phys_map_type_t type, uint64_t start, size_t length) {
    spinlock_acquire(&g_phys_map_lock);
    phys_map_convert_locked(type, start, length);
    spinlock_release(&g_phys_map_lock);
}

err_t phys_map_get_type(uint64_t start, size_t length, phys_map_type_t* type) {
    err_t err = NO_ERROR;

    spinlock_acquire(&g_phys_map_lock);

    uint64_t top_address = 0;
    CHECK(!__builtin_add_overflow(start, length, &top_address));

    rb_node_t* found = rb_find(&start, &g_phys_map, phys_map_cmp_contains);
    CHECK_ERROR(found != NULL, ERROR_NOT_FOUND);

    phys_map_entry_t* entry = rb_entry(found, phys_map_entry_t, node);
    CHECK_ERROR(top_address < entry->end, ERROR_NOT_FOUND);

    *type = entry->type;

cleanup:
    spinlock_release(&g_phys_map_lock);

    return err;
}

INIT_DATA static const phys_map_type_t m_limine_memmap_type[] = {
    [LIMINE_MEMMAP_USABLE] = PHYS_MAP_RAM,
    [LIMINE_MEMMAP_RESERVED] = PHYS_MAP_FIRMWARE_RESERVED,
    [LIMINE_MEMMAP_ACPI_RECLAIMABLE] = PHYS_MAP_ACPI_RECLAIMABLE,
    [LIMINE_MEMMAP_ACPI_NVS] = PHYS_MAP_ACPI_NVS,
    [LIMINE_MEMMAP_BAD_MEMORY] = PHYS_MAP_BAD_RAM,
    [LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE] = PHYS_MAP_BOOTLOADER_RECLAIMABLE,
    [LIMINE_MEMMAP_EXECUTABLE_AND_MODULES] = PHYS_MAP_KERNEL_RESERVED,
    [LIMINE_MEMMAP_FRAMEBUFFER] = PHYS_MAP_MMIO_FRAMEBUFFER,
    [LIMINE_MEMMAP_RESERVED_MAPPED] = PHYS_MAP_FIRMWARE_RESERVED,
};


static uint8_t get_physical_address_bits(void) {
    CPUID_VIR_PHY_ADDRESS_SIZE_EAX eax = {};
    uint32_t b, c, d;
    if (__get_cpuid(CPUID_VIR_PHY_ADDRESS_SIZE, &eax.raw, &b, &c, &d)) {
        return eax.PHYS_ADDR_SIZE;
    }
    return 0;
}

INIT_CODE err_t init_phys_map(void) {
    err_t err = NO_ERROR;

    // setup the allocator
    mem_alloc_init(&m_phys_map_alloc, sizeof(phys_map_entry_t), alignof(phys_map_entry_t));

    // Get the physical address bits and set the entire range as unused
    // We verify we can also map that entire physical memory space in the higher half
    uint8_t physical_address_bits = get_physical_address_bits();
    phys_map_convert(PHYS_MAP_UNUSED, 0, 1ull << physical_address_bits);

    struct limine_memmap_response* response = g_limine_memmap_request.response;
    for (int i = 0; i < response->entry_count; i++) {
        struct limine_memmap_entry* entry = response->entries[i];

        // get the phys map type, for unknown entries just mark as firmware reserved
        phys_map_type_t type = PHYS_MAP_FIRMWARE_RESERVED;
        if (entry->type < ARRAY_LENGTH(m_limine_memmap_type)) {
            type = m_limine_memmap_type[entry->type];
            CHECK(type != PHYS_MAP_UNUSED);
        }

        // and convert the range, this time we should have enough memory to allocate entries
        phys_map_convert(type, entry->base, entry->length);
    }

cleanup:
    return err;
}

err_t phys_map_iterate(phys_map_cb_t cb, void* ctx) {
    err_t err = NO_ERROR;

    spinlock_acquire(&g_phys_map_lock);

    for (rb_node_t* n = rb_first(&g_phys_map); n != NULL; n = rb_next(n)) {
        phys_map_entry_t* entry = rb_entry(n, phys_map_entry_t, node);
        err = cb(ctx, entry->type, entry->start, (entry->end - entry->start) + 1);
        if (err == END_ITERATION) {
            break;
        }
        RETHROW(err);
    }

cleanup:
    spinlock_release(&g_phys_map_lock);

    return err;
}

static const char* m_phys_map_type_str[] = {
    [PHYS_MAP_UNUSED] = "<unused>",
    [PHYS_MAP_BAD_RAM] = "Reserved (Bad RAM)",
    [PHYS_MAP_RAM] = "RAM",

    [PHYS_MAP_MMIO] = "MMIO",
    [PHYS_MAP_MMIO_LAPIC] = "MMIO (Local-APIC)",
    [PHYS_MAP_MMIO_FRAMEBUFFER] = "MMIO (Framebuffer)",

    [PHYS_MAP_FIRMWARE_RESERVED] = "Reserved (Firmware)",

    [PHYS_MAP_ACPI_RECLAIMABLE] = "Reclaimable (ACPI)",
    [PHYS_MAP_ACPI_NVS] = "Reserved (ACPI NVS)",

    [PHYS_MAP_BOOTLOADER_RECLAIMABLE] = "Reclaimable (Bootloader)",
    [PHYS_MAP_KERNEL_RESERVED] = "Kernel reserved",
};

static err_t phys_dump_entry(void* ctx, phys_map_type_t type, uint64_t start, size_t length) {
    TRACE("memory: \t%016lx-%016lx: %s", start, start + length - 1, m_phys_map_type_str[type]);
    return NO_ERROR;
}

void phys_map_dump(void) {
    TRACE("memory: Physical memory map:");
    (void)phys_map_iterate(phys_dump_entry, NULL);
}
