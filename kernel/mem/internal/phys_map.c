#include "phys_map.h"

#include <cpuid.h>

#include "limine.h"
#include "limine_requests.h"
#include "mem/kernel/alloc.h"
#include "arch/cpuid.h"


list_t g_phys_map = LIST_INIT(&g_phys_map);
irq_spinlock_t g_phys_map_lock = IRQ_SPINLOCK_INIT;

/**
 * Allocate phys map entry
 */
static phys_map_entry_t* allocate_phys_map_entry(void) {
    return alloc_type(phys_map_entry_t);
}

static void phys_map_insert_new_entry(list_entry_t* link, uint64_t start, uint64_t end, phys_map_type_t type, bool next) {
    // allocate an entry
    phys_map_entry_t* entry = allocate_phys_map_entry();
    ASSERT(entry != NULL);

    // set it up
    entry->type = type;
    entry->start = start;
    entry->end = end;
    if (next) {
        list_add(link, &entry->link);
    } else {
        list_add_tail(link, &entry->link);
    }
}

static void phys_map_remove_old_entry(phys_map_entry_t* entry) {
    // remove from the linked list
    list_del(&entry->link);
    free_type(phys_map_entry_t, entry);
}

void phys_map_convert_locked(phys_map_type_t type, uint64_t start, size_t length) {
    uint64_t end = start + length - 1;

    list_entry_t* link = g_phys_map.next;
    while (link != &g_phys_map) {
        phys_map_entry_t* entry = containerof(link, phys_map_entry_t, link);
        link = link->next;

        //
        // ---------------------------------------------------
        // |  +----------+   +------+   +------+   +------+  |
        // ---|m_phys_map|---|Entry1|---|Entry2|---|Entry3|---
        //    +----------+ ^ +------+   +------+   +------+
        //                 |
        //              +------+
        //              |EntryX|
        //              +------+
        //
        if (entry->start > end) {
            if ((entry->start == end + 1) && (entry->type == type)) {
                entry->start = start;
                return;
            }

            phys_map_insert_new_entry(&entry->link, start, end, type, false);
            return;
        }

        if ((entry->start <= start) && (entry->end >= end)) {
            if (entry->type != type) {
                if (entry->start < start) {
                    //
                    // ---------------------------------------------------
                    // |  +----------+   +------+   +------+   +------+  |
                    // ---|m_phys_map|---|Entry1|---|EntryX|---|Entry3|---
                    //    +----------+   +------+ ^ +------+   +------+
                    //                            |
                    //                         +------+
                    //                         |EntryA|
                    //                         +------+
                    //
                    phys_map_insert_new_entry(&entry->link, entry->start, start - 1, entry->type, false);
                }

                if (entry->end > end) {
                    //
                    // ---------------------------------------------------
                    // |  +----------+   +------+   +------+   +------+  |
                    // ---|m_phys_map|---|Entry1|---|EntryX|---|Entry3|---
                    //    +----------+   +------+   +------+ ^ +------+
                    //                                       |
                    //                                    +------+
                    //                                    |EntryZ|
                    //                                    +------+
                    //
                    phys_map_insert_new_entry(&entry->link, end + 1, entry->end, entry->type, true);
                }

                //
                // Update this node
                //
                entry->start = start;
                entry->end = end;
                entry->type = type;

                //
                // Check adjacent
                //
                list_entry_t* next_link = entry->link.next;
                if (next_link != &g_phys_map) {
                    phys_map_entry_t* next_entry = containerof(next_link, phys_map_entry_t, link);
                    //
                    // ---------------------------------------------------
                    // |  +----------+   +------+   +-----------------+  |
                    // ---|m_phys_map|---|Entry1|---|EntryX     Entry3|---
                    //    +----------+   +------+   +-----------------+
                    //
                    if ((entry->type == next_entry->type) && (entry->end + 1 == next_entry->start)) {
                        entry->end = next_entry->end;
                        phys_map_remove_old_entry(next_entry);
                    }
                }

                list_entry_t* prev_link = entry->link.prev;
                if (prev_link != &g_phys_map) {
                    phys_map_entry_t* prev_entry = containerof(prev_link, phys_map_entry_t, link);
                    //
                    // ---------------------------------------------------
                    // |  +----------+   +-----------------+   +------+  |
                    // ---|m_phys_map|---|Entry1     EntryX|---|Entry3|---
                    //    +----------+   +-----------------+   +------+
                    //
                    if ((prev_entry->type == entry->type) && (prev_entry->end + 1 == entry->start)) {
                        prev_entry->end = entry->end;
                        phys_map_remove_old_entry(entry);
                    }
                }
            }

            return;
        }
    }

    //
    // ---------------------------------------------------
    // |  +----------+   +------+   +------+   +------+  |
    // ---|m_phys_map|---|Entry1|---|Entry2|---|Entry3|---
    //    +----------+   +------+   +------+   +------+ ^
    //                                                  |
    //                                               +------+
    //                                               |EntryX|
    //                                               +------+
    //
    link = g_phys_map.prev;
    if (link != &g_phys_map) {
        phys_map_entry_t* entry = containerof(link, phys_map_entry_t, link);
        if ((entry->end + 1 == start) && (entry->type == type)) {
            entry->end = end;
            return;
        }
    }

    phys_map_insert_new_entry(&g_phys_map, start, end, type, false);
}

void phys_map_convert(phys_map_type_t type, uint64_t start, size_t length) {
    bool irq_lock = irq_spinlock_acquire(&g_phys_map_lock);
    phys_map_convert_locked(type, start, length);
    irq_spinlock_release(&g_phys_map_lock, irq_lock);
}

err_t phys_map_get_type(uint64_t start, size_t length, phys_map_type_t* type) {
    err_t err = NO_ERROR;

    bool irq_state = irq_spinlock_acquire(&g_phys_map_lock);

    uint64_t top_address = 0;
    CHECK(!__builtin_add_overflow(start, length, &top_address));

    phys_map_entry_t* entry;
    list_for_each_entry(entry, &g_phys_map, link) {
        if (entry->start <= start && top_address < entry->end) {
            *type = entry->type;
            goto cleanup;
        }
    }

    CHECK_FAIL_ERROR(ERROR_NOT_FOUND);

cleanup:
    irq_spinlock_release(&g_phys_map_lock, irq_state);

    return err;
}

static const phys_map_type_t m_limine_memmap_type[] = {
    [LIMINE_MEMMAP_USABLE] = PHYS_MAP_RAM,
    [LIMINE_MEMMAP_RESERVED] = PHYS_MAP_FIRMWARE_RESERVED,
    [LIMINE_MEMMAP_ACPI_RECLAIMABLE] = PHYS_MAP_ACPI_RECLAIMABLE,
    [LIMINE_MEMMAP_ACPI_NVS] = PHYS_MAP_ACPI_NVS,
    [LIMINE_MEMMAP_BAD_MEMORY] = PHYS_MAP_BAD_RAM,
    [LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE] = PHYS_MAP_BOOTLOADER_RECLAIMABLE,
    [LIMINE_MEMMAP_EXECUTABLE_AND_MODULES] = PHYS_MAP_KERNEL_RESERVED,
    [LIMINE_MEMMAP_FRAMEBUFFER] = PHYS_MAP_MMIO_FRAMEBUFFER,
    [LIMINE_MEMMAP_ACPI_TABLES] = PHYS_MAP_ACPI_RESERVED,
};

err_t init_phys_map(void) {
    err_t err = NO_ERROR;

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

    bool irq_state = irq_spinlock_acquire(&g_phys_map_lock);

    phys_map_entry_t* entry;
    list_for_each_entry(entry, &g_phys_map, link) {
        err = cb(ctx, entry->type, entry->start, (entry->end - entry->start) + 1);
        if (err == END_ITERATION) {
            break;
        }
        RETHROW(err);
    }

cleanup:
    irq_spinlock_release(&g_phys_map_lock, irq_state);

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
    [PHYS_MAP_ACPI_RESERVED] = "Reserved (ACPI)",
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

uint8_t get_physical_address_bits(void) {
    CPUID_VIR_PHY_ADDRESS_SIZE_EAX eax = {};
    uint32_t b, c, d;
    if (__get_cpuid(CPUID_VIR_PHY_ADDRESS_SIZE, &eax.packed, &b, &c, &d)) {
        return eax.physical_address_bits;
    }
    return 0;
}
