#include "phys.h"

#include <cpuid.h>
#include <stdckdint.h>
#include <limits.h>

#include "limine_requests.h"
#include "memory.h"
#include "phys.h"

#include "alloc.h"
#include "arch/cpuid.h"
#include "lib/list.h"
#include "lib/string.h"
#include "lib/rbtree/rbtree.h"
#include "sync/spinlock.h"

typedef struct phys_map_entry {
    // link list of entire memory map
    list_entry_t link;

    // the actual range
    uint64_t start;
    uint64_t end;
    phys_map_type_t type;
} phys_map_entry_t;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Physical buddy allocator
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * How many we have in the buddy, this affects how much
 * can actually be allocated
 */
#define BUDDY_MAX_LEVEL     15

/**
 * The min order of the buddy, we only allow to allocate at
 * page multiplies
 */
#define BUDDY_MIN_ORDER     PAGE_SHIFT
#define BUDDY_MIN_SIZE      (1UL << BUDDY_MIN_ORDER)

/**
 * The maximum size the buddy can handle
 */
#define BUDDY_MAX_SIZE      (1ULL << ((BUDDY_MAX_LEVEL + BUDDY_MIN_ORDER) - 1))

typedef struct buddy_level {
    list_t freelist;
} buddy_level_t;

typedef struct phys_buddy {
    /**
     * Link in the list of buddies
     */
    list_entry_t link;

    /**
     * The freelist levels
     */
    buddy_level_t levels[BUDDY_MAX_LEVEL];

    /**
     * The base and end of the buddy
     */
    void* base;
    void* end;

    /**
     * The bitmap that tracks if pages can be merged
     * we use 4 bits per page where:
     * - 0 .. 14: the level of the freed page
     * - 15: the page is currently allocated
     */
    uint8_t bitmap[];
} phys_buddy_t;

/**
 * List of buddy allocators that are currently available
 * TODO: maybe replace with a vector/rbtree?
 */
static list_t m_buddy_list = LIST_INIT(&m_buddy_list);

/**
 * Lock to protect the buddy list
 */
static irq_spinlock_t m_buddy_lock = IRQ_SPINLOCK_INIT;

/**
 * Get the index within the buddy for the given base
 */
static size_t buddy_get_page_index(phys_buddy_t* buddy, void* addr) {
    return (addr - buddy->base) / PAGE_SIZE;
}

/**
 * Get the page metadata for the given address in the buddy
 */
static uint8_t buddy_get_page_metadata(phys_buddy_t* buddy, void* addr) {
    size_t index = buddy_get_page_index(buddy, addr);
    size_t shift = ((index & 1) * 4);
    return (buddy->bitmap[index / 2] >> shift) & 0xF;
}

/**
 * Set the given address as allocated in the buddy
 */
static void buddy_set_page_allocated(phys_buddy_t* buddy, void* addr) {
    size_t index = buddy_get_page_index(buddy, addr);
    size_t shift = ((index & 1) * 4);
    buddy->bitmap[index / 2] |= 0xF << shift;
}

/**
 * Set the given address as free (with the given level)
 */
static void buddy_set_page_free(phys_buddy_t* buddy, void* addr, int level) {
    size_t index = buddy_get_page_index(buddy, addr);
    size_t shift = ((index & 1) * 4);
    buddy->bitmap[index / 2] &= ~(0xF << shift);
    buddy->bitmap[index / 2] |= level << shift;
}

static int get_buddy_level(void* start, void* end) {
    uintptr_t addr = (uintptr_t)start;
    uintptr_t addr_end = (uintptr_t)end;

    for (int i = BUDDY_MAX_LEVEL - 1; i >= 0; i--) {
        // check we have enough space for this level
        size_t size = 1ULL << (i + BUDDY_MIN_ORDER);
        if (addr + size > addr_end) {
            continue;
        }

        // check the alignment matches
        size_t alignment = size - 1;
        if ((addr & alignment) == 0) {
            return i;
        }
    }
    ASSERT(false);
}

static void phys_buddy_create(phys_map_entry_t* entry) {
    // we assume the ram regions are aligned properly
    ASSERT((entry->start % PAGE_SIZE) == 0);
    ASSERT(((entry->end + 1) % PAGE_SIZE) == 0);

    // calculate the metadata overhead
    size_t page_count = ((entry->end + 1) - entry->start) / PAGE_SIZE;
    size_t bitmap_size = DIV_ROUND_UP(page_count, 2);
    size_t metadata_size = ALIGN_UP(sizeof(phys_buddy_t) + bitmap_size, PAGE_SIZE);

    // setup the buddy itself
    phys_buddy_t* buddy = PHYS_TO_DIRECT(entry->start);
    for (int i = 0; i < ARRAY_LENGTH(buddy->levels); i++) {
        list_init(&buddy->levels[i].freelist);
    }
    memset(buddy->bitmap, 0, bitmap_size);
    list_add_tail(&m_buddy_list, &buddy->link);

    // add all of the stuff to the freelist
    void* data_start = PHYS_TO_DIRECT(entry->start) + metadata_size;
    void* data_end = PHYS_TO_DIRECT(entry->end + 1);

    // save the start so we can calculate bitmap indexes more easily
    buddy->base = data_start;
    buddy->end = data_end;

    // add everything to the correct free-lists
    while (data_start < data_end) {
        int level = get_buddy_level(data_start, data_end);
        size_t size = 1ULL << (level + BUDDY_MIN_ORDER);
        list_entry_t* block = data_start;
        list_add(&buddy->levels[level].freelist, block);
        data_start += size;
    }
}

static int get_level_by_size(size_t size) {
    // allocation is too big, return invalid
    if (size > BUDDY_MAX_SIZE) {
        return -1;
    }

    // allocation is too small, round to 4kb
    if (size < BUDDY_MIN_SIZE) {
        size = BUDDY_MIN_SIZE;
    }

    // align up to next power of two
    size = 1 << (32 - __builtin_clz(size - 1));

    // and now calculate the log2
    int level = 32 - __builtin_clz(size) - 1;

    // ignore the first 12 levels because they are less than 4kb
    return level - BUDDY_MIN_ORDER;
}

void* phys_alloc(size_t size) {
    int level = get_level_by_size(size);
    if (level < 0) {
        ERROR("memory: too much memory requested (0x%lx bytes)", size);
        return NULL;
    }

    bool irq_state = irq_spinlock_acquire(&m_buddy_lock);

    // find a buddy that has a free page at the smallest level that is above or equal
    // to the level that we want, we are going to iterate all the buddies each time
    // to avoid splitting without needing to
    int block_at_level = 0;
    phys_buddy_t* buddy = NULL;
    void* block = NULL;
    for (block_at_level = level; block_at_level < BUDDY_MAX_LEVEL; block_at_level++) {
        phys_buddy_t* entry;
        list_for_each_entry(entry, &m_buddy_list, link) {
            block = list_pop(&entry->levels[block_at_level].freelist);
            if (block != NULL) {
                buddy = entry;
                break;
            }
        }

        if (buddy != NULL) {
            break;
        }
    }

    if (buddy != NULL) {
        ASSERT(block != NULL);

        // split the blocks until we reach
        // the requested level
        while (block_at_level > level) {
            // we need the size to split it
            size_t block_size = 1ULL << (block_at_level + BUDDY_MIN_ORDER);
            block_at_level--;

            // add the upper part of the page to the bottom freelist
            list_entry_t* upper = block + block_size / 2;
            list_add(&buddy->levels[block_at_level].freelist, upper);

            // mark the upper block as the new it is at
            buddy_set_page_free(buddy, upper, block_at_level);
        }

        // mark our block as allocated
        buddy_set_page_allocated(buddy, block);
    }

    irq_spinlock_release(&m_buddy_lock, irq_state);

    return block;
}

void phys_free(void* ptr, size_t size) {
    if (ptr == NULL) {
        return;
    }

    int level = get_level_by_size(size);
    ASSERT(level >= 0);

    // sanity check
    ASSERT(((uintptr_t)ptr % (1UL << level)) == 0);

    bool irq_state = irq_spinlock_acquire(&m_buddy_lock);

    // find the buddy this pointer is part of
    phys_buddy_t* buddy = NULL;
    phys_buddy_t* entry;
    list_for_each_entry(entry, &m_buddy_list, link) {
        if (entry->base <= ptr && ptr < entry->end) {
            buddy = entry;
            break;
        }
    }
    ASSERT(buddy != NULL);

    while (level < (BUDDY_MAX_LEVEL - 1)) {
        size_t block_size = 1UL << (level + BUDDY_MIN_ORDER);

        void* neighbor = (void*)((uintptr_t)ptr ^ block_size);
        if (buddy->base > neighbor && neighbor + block_size >= buddy->end) {
            // neighbor is outside the buddy region, we can't merge with it even if we want to++
            break;
        }

        uint8_t metadata = buddy_get_page_metadata(buddy, neighbor);
        if (metadata != level) {
            // the neighbor is either allocated or is not free
            // at the same level as the current block we are freeing
            // so we can't merge with it
            break;
        }

        // remove it from the freelist
        list_del(neighbor);

        // if the neighbor is from the bottom
        // then merge with it from the bottom
        if (ptr > neighbor) {
            ptr = neighbor;
        }

        // next level please
        level++;
    }

    // we merged it as much as we can, mark the page as free with the correct level
    // and add it to the freelist
    buddy_set_page_free(buddy, ptr, level);
    list_add(&buddy->levels[level].freelist, &buddy->link);

    irq_spinlock_release(&m_buddy_lock, irq_state);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Physical memory map
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * The linked list of memory map ranges
 */
static list_t m_phys_map = LIST_INIT(&m_phys_map);

/**
 * Lock to protect the physical memory map
 */
static irq_spinlock_t m_phys_map_lock = IRQ_SPINLOCK_INIT;

/**
 * We have a small range of descriptors pre-allocated to be used
 * before the buddy is initialized (or when we have enough memory that the
 * buddy is not even needed)
 */
static size_t m_entry_bitmap = 0;
static phys_map_entry_t m_phys_map_alloc[sizeof(size_t) * 8] = {};

/**
 * Freelist of allocated entries
 */
static list_t m_entry_free_list = LIST_INIT(&m_entry_free_list);

/**
 * Allocate phys map entry
 */
static phys_map_entry_t* allocate_phys_map_entry(void) {
    // if we are out of ranges in the static allocation
    // then allocate from the phys allocator
    if (m_entry_bitmap == SIZE_MAX) {
        phys_map_entry_t* entry = alloc_type(phys_map_entry_t);
        if (entry != NULL) {
            *entry = (phys_map_entry_t){};
        }
        return entry;
    }

    // find the first unused bit and return it
    size_t index = __builtin_ctzl(~m_entry_bitmap);
    m_entry_bitmap |= 1UL << index;
    phys_map_entry_t* entry = &m_phys_map_alloc[index];
    return entry;
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

    if (type == PHYS_MAP_RAM) {
        phys_buddy_create(entry);
    }
}

static void phys_map_remove_old_entry(phys_map_entry_t* entry) {
    // remove from the linked list
    list_del(&entry->link);

    // free
    if (&m_phys_map_alloc[0] <= entry && entry < &m_phys_map_alloc[ARRAY_LENGTH(m_phys_map_alloc)]) {
        // from the heap, free it normally
        memset(entry, 0, sizeof(*entry));
        m_entry_bitmap &= ~(1UL << (entry - m_phys_map_alloc));
    } else {
        // from the heap
        list_add(&m_entry_free_list, &entry->link);
    }
}

static void phys_map_convert_locked(phys_map_type_t type, uint64_t start, size_t length) {
    uint64_t end = start + length - 1;

    list_entry_t* link = m_phys_map.next;
    while (link != &m_phys_map) {
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
                if (type == PHYS_MAP_RAM) {
                    ASSERT(!"TODO: resize buddy downwards");
                }
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

                bool create_buddy = type == PHYS_MAP_RAM && true;

                //
                // Check adjacent
                //
                list_entry_t* next_link = entry->link.next;
                if (next_link != &m_phys_map) {
                    phys_map_entry_t* next_entry = containerof(next_link, phys_map_entry_t, link);
                    //
                    // ---------------------------------------------------
                    // |  +----------+   +------+   +-----------------+  |
                    // ---|m_phys_map|---|Entry1|---|EntryX     Entry3|---
                    //    +----------+   +------+   +-----------------+
                    //
                    if ((entry->type == next_entry->type) && (entry->end + 1 == next_entry->start)) {
                        if (type == PHYS_MAP_RAM) {
                            // the next entry has a buddy, join with it
                            create_buddy = false;
                            WARN("TODO: need to merge with next entry");
                        }

                        entry->end = next_entry->end;
                        phys_map_remove_old_entry(next_entry);
                    }
                }

                list_entry_t* prev_link = entry->link.prev;
                if (prev_link != &m_phys_map) {
                    phys_map_entry_t* prev_entry = containerof(prev_link, phys_map_entry_t, link);
                    //
                    // ---------------------------------------------------
                    // |  +----------+   +-----------------+   +------+  |
                    // ---|m_phys_map|---|Entry1     EntryX|---|Entry3|---
                    //    +----------+   +-----------------+   +------+
                    //
                    if ((prev_entry->type == entry->type) && (prev_entry->end + 1 == entry->start)) {
                        if (type == PHYS_MAP_RAM) {
                            // already has a buddy in the previous link, join with it
                            create_buddy = false;
                            WARN("TODO: need to merge with previous entry");
                        }
                        prev_entry->end = entry->end;
                        phys_map_remove_old_entry(entry);
                    }
                }

                if (create_buddy) {
                    // setup the buddy
                    phys_buddy_create(entry);
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
    link = m_phys_map.prev;
    if (link != &m_phys_map) {
        phys_map_entry_t* entry = containerof(link, phys_map_entry_t, link);
        if ((entry->end + 1 == start) && (entry->type == type)) {
            entry->end = end;
            if (type == PHYS_MAP_RAM) {
                ASSERT(!"TODO: resize buddy forward");
            }
            return;
        }
    }

    phys_map_insert_new_entry(&m_phys_map, start, end, type, false);
}

void phys_map_convert(phys_map_type_t type, uint64_t start, size_t length) {
    bool irq_lock = irq_spinlock_acquire(&m_phys_map_lock);
    phys_map_convert_locked(type, start, length);
    irq_spinlock_release(&m_phys_map_lock, irq_lock);
}

err_t phys_map_get_type(uint64_t start, size_t length, phys_map_type_t* type) {
    err_t err = NO_ERROR;

    bool irq_state = irq_spinlock_acquire(&m_buddy_lock);

    uint64_t top_address = 0;
    CHECK(!ckd_add(&top_address, start, length));

    phys_map_entry_t* entry;
    list_for_each_entry(entry, &m_phys_map, link) {
        if (entry->start <= start && top_address < entry->end) {
            *type = entry->type;
            goto cleanup;
        }
    }

    CHECK_FAIL_ERROR(ERROR_NOT_FOUND);

cleanup:
    irq_spinlock_release(&m_buddy_lock, irq_state);

    return err;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Phys initialization code
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

uint8_t get_physical_address_bits(void) {
    CPUID_VIR_PHY_ADDRESS_SIZE_EAX eax = {};
    uint32_t b, c, d;
    if (__get_cpuid(CPUID_VIR_PHY_ADDRESS_SIZE, &eax.packed, &b, &c, &d)) {
        return eax.physical_address_bits;
    }
    return 0;
}

err_t phys_init(void) {
    err_t err = NO_ERROR;

    // Get the physical address bits and set the entire range as unused
    // We verify we can also map that entire physical memory space in the higher half
    uint8_t physical_address_bits = get_physical_address_bits();
    CHECK(physical_address_bits > 0);
    CHECK(physical_address_bits <= 46);
    phys_map_convert(PHYS_MAP_UNUSED, 0, 1ull << physical_address_bits);

    // ensure we have the physical memory map from limine
    struct limine_memmap_response* response = g_limine_memmap_request.response;
    CHECK(response->entry_count > 0);

    // now we can add the other ranges
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

void phys_free_bootloader_reserved(void) {
    bool irq_state = irq_spinlock_acquire(&m_phys_map_lock);

    // we are going to attempt and convert entries until we can't find
    // a matching entry
    TRACE("memory: Reclaiming bootloader memory");
    while (true) {
        // check if we have an entry that matches the wanted type
        phys_map_entry_t* entry;
        phys_map_entry_t* match = NULL;
        list_for_each_entry(entry, &m_phys_map, link) {
            if (entry->type == PHYS_MAP_BOOTLOADER_RECLAIMABLE) {
                match = entry;
                break;
            }
        }

        // no match, we are done
        if (match == NULL) {
            break;
        }

        // create a copy before we continue since the convert
        // will cause the entry to be freed
        phys_map_entry_t copy = *match;
        match = NULL;

        // and convert it to normal ram
        TRACE("memory: \t%016lx-%016lx", copy.start, copy.end);
        phys_map_convert_locked(PHYS_MAP_RAM, copy.start, copy.end - copy.start + 1);
    }

    irq_spinlock_release(&m_phys_map_lock, irq_state);
}

const char* g_phys_map_type_str[] = {
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

err_t phys_map_iterate(phys_map_cb_t cb, void* ctx) {
    err_t err = NO_ERROR;

    bool irq_state = irq_spinlock_acquire(&m_phys_map_lock);

    phys_map_entry_t* entry;
    list_for_each_entry(entry, &m_phys_map, link) {
        RETHROW(cb(ctx, entry->type, entry->start, (entry->end - entry->start) + 1));
    }

cleanup:
    irq_spinlock_release(&m_phys_map_lock, irq_state);
    return err;
}

static err_t phys_dump_entry(void* ctx, phys_map_type_t type, uint64_t start, size_t length) {
    TRACE("memory: \t%016lx-%016lx: %s", start, start + length - 1, g_phys_map_type_str[type]);
    return NO_ERROR;
}

void dump_phys_map(void) {
    (void)phys_map_iterate(phys_dump_entry, NULL);
}
