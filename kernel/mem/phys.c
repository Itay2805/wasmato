#include "phys.h"

#include <cpuid.h>
#include <stdckdint.h>
#include <limits.h>

#include "limine_requests.h"
#include "memory.h"
#include "phys.h"

#include "alloc.h"
#include "early.h"
#include "phys_map.h"
#include "arch/cpuid.h"
#include "lib/list.h"
#include "lib/string.h"
#include "lib/rbtree/rbtree.h"
#include "sync/spinlock.h"

typedef struct buddy_free_page {
    /**
     * Link in the freelist
     */
    list_entry_t entry;

    /**
     * The level of this page
     */
    uint8_t level;
} buddy_free_page_t;

/**
 * Represents a single buddy level
 */
typedef struct buddy_level {
    list_t freelist;
} buddy_level_t;

/**
 * The buddy levels of the buddy allocator
 */
static buddy_level_t m_phys_buddy_levels[PHYS_BUDDY_MAX_LEVEL] = {};

/**
 * Lock to protect the buddy list
 */
static irq_spinlock_t m_phys_buddy_lock = IRQ_SPINLOCK_INIT;

static int get_level_by_size(size_t size) {
    // allocation is too big, return invalid
    if (size > PHYS_BUDDY_MAX_SIZE) {
        return -1;
    }

    // allocation is too small, round to 4kb
    if (size < PHYS_BUDDY_MIN_SIZE) {
        size = PHYS_BUDDY_MIN_SIZE;
    }

    // align up to next power of two
    size = 1 << (32 - __builtin_clz(size - 1));

    // and now calculate the log2
    int level = 32 - __builtin_clz(size) - 1;

    // ignore the first 12 levels because they are less than 4kb
    return level - PHYS_BUDDY_MIN_ORDER;
}

static bool buddy_is_block_free(void* ptr) {
    uintptr_t addr = DIRECT_TO_PHYS(ptr);
    size_t index = (addr / PAGE_SIZE) / 8;
    size_t shift = (addr / PAGE_SIZE) % 8;
    return (PHYS_BUDDY_BITMAP_START[index] >> shift) & 1;
}

static void buddy_set_block_allocated(void* ptr) {
    uintptr_t addr = DIRECT_TO_PHYS(ptr);
    size_t index = (addr / PAGE_SIZE) / 8;
    size_t shift = (addr / PAGE_SIZE) % 8;
    PHYS_BUDDY_BITMAP_START[index] &= ~(1U << shift);
}

static void buddy_set_block_free(void* ptr) {
    uintptr_t addr = DIRECT_TO_PHYS(ptr);
    size_t index = (addr / PAGE_SIZE) / 8;
    size_t shift = (addr / PAGE_SIZE) % 8;
    PHYS_BUDDY_BITMAP_START[index] |= 1U << shift;
}

void* phys_alloc(size_t size) {
    int level = get_level_by_size(size);
    if (level < 0) {
        ERROR("memory: too much memory requested (0x%lx bytes)", size);
        return NULL;
    }

    bool irq_state = irq_spinlock_acquire(&m_phys_buddy_lock);
    unlock_direct_map();

    // search for a free page in the freelists that has the closest level to what we want
    int block_at_level = 0;
    void* block = NULL;
    for (block_at_level = level; block_at_level < PHYS_BUDDY_MAX_LEVEL; block_at_level++) {
        list_t* freelist = &m_phys_buddy_levels[block_at_level].freelist;
        if (!list_is_empty(freelist)) {
            buddy_free_page_t* page = list_first_entry(freelist, buddy_free_page_t, entry);
            ASSERT(page->level == block_at_level);
            list_del(&page->entry);
            block = page;
            break;
        }
    }

    if (block != NULL) {
        // split the blocks until we reach
        // the requested level
        while (block_at_level > level) {
            // we need the size to split it
            size_t block_size = 1ULL << (block_at_level + PHYS_BUDDY_MIN_ORDER);
            block_at_level--;

            // add the upper part of the page to the bottom freelist
            buddy_free_page_t* upper = block + block_size / 2;
            upper->level = block_at_level;
            list_add(&m_phys_buddy_levels[block_at_level].freelist, &upper->entry);
            buddy_set_block_free(upper);
        }

        // mark our block as allocated
        buddy_set_block_allocated(block);
    }

    lock_direct_map();
    irq_spinlock_release(&m_phys_buddy_lock, irq_state);

    return block;
}

static void phys_free_internal(void* ptr, int level, bool check_allocated) {
    // sanity check
    ASSERT(((uintptr_t)ptr % (1UL << level)) == 0);

    bool irq_state = irq_spinlock_acquire(&m_phys_buddy_lock);
    unlock_direct_map();

    // mark the block as free right away
    if (check_allocated) {
        ASSERT(!buddy_is_block_free(ptr));
        buddy_set_block_free(ptr);
    }

    // go up the levels and search for other free blocks
    // that we can merge with
    while (level < (PHYS_BUDDY_MAX_LEVEL - 1)) {
        size_t block_size = 1UL << (level + PHYS_BUDDY_MIN_ORDER);

        buddy_free_page_t* neighbor = (void*)((uintptr_t)ptr ^ block_size);

        // we can only merge with a free block
        if (!buddy_is_block_free(neighbor)) {
            break;
        }

        // we can only merge with a block that is the
        // same level as us
        if (neighbor->level != level) {
            break;
        }

        // remove it from the freelist
        list_del(&neighbor->entry);

        // if the neighbor is from the bottom
        // then merge with it from the bottom
        if (ptr > (void*)neighbor) {
            ptr = neighbor;
        }

        // next level please
        level++;
    }

    // we merged it as much as we can, add to the freelist
    buddy_free_page_t* block = ptr;
    block->level = level;
    list_add(&m_phys_buddy_levels[level].freelist, &block->entry);
    buddy_set_block_free(block);

    lock_direct_map();
    irq_spinlock_release(&m_phys_buddy_lock, irq_state);
}

void phys_free(void* ptr, size_t size) {
    if (ptr == NULL) {
        return;
    }

    int level = get_level_by_size(size);
    ASSERT(level >= 0);

    phys_free_internal(ptr, level, true);
}

//----------------------------------------------------------------------------------------------------------------------
// Buddy initialization
//----------------------------------------------------------------------------------------------------------------------

static int get_best_level_for_block(void* start, void* end) {
    uintptr_t addr = (uintptr_t)start;
    uintptr_t addr_end = (uintptr_t)end;

    for (int i = PHYS_BUDDY_MAX_LEVEL - 1; i >= 0; i--) {
        // check we have enough space for this level
        size_t size = 1ULL << (i + PHYS_BUDDY_MIN_ORDER);
        if (addr + size > addr_end) {
            continue;
        }

        // check the alignment matches
        size_t alignment = size - 1;
        if ((addr & alignment) == 0) {
            return i;
        }
    }
    return -1;
}

static err_t phys_add_memory(void* start, void* end) {
    err_t err = NO_ERROR;

    while (start < end) {
        // get the best level that fits the block
        int level = get_best_level_for_block(start, end);
        CHECK(level >= 0);

        // free it, the logic should just work
        phys_free_internal(start, level, false);

        // next block
        size_t block_size = 1ULL << (level + PHYS_BUDDY_MIN_ORDER);
        start += block_size;
    }

cleanup:
    return err;
}

err_t init_phys(void) {
    err_t err = NO_ERROR;

    // initialize the freelists
    for (int i = 0; i < PHYS_BUDDY_MAX_LEVEL; i++) {
        list_init(&m_phys_buddy_levels[i].freelist);
    }

    // map all the ranges now
    struct limine_memmap_response* response = g_limine_memmap_request.response;
    CHECK(response != NULL);

    void* early_alloc_top = early_alloc_get_top();

    // add all the blocks marked as usable
    TRACE("memory: Adding usable memory");
    for (int i = 0; i < response->entry_count; i++) {
        struct limine_memmap_entry* entry = response->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            void* start = PHYS_TO_DIRECT(entry->base);
            void* end = PHYS_TO_DIRECT(entry->base + entry->length);

            // if this is below the early allocator
            // then its already in use
            if (early_alloc_top >= end) {
                TRACE("memory: \t%016lx-%016lx: used by early allocator", DIRECT_TO_PHYS(start), DIRECT_TO_PHYS(end) - 1);
                continue;
            }

            // if the start is below the bump then start freeing
            // from the bump
            if (start < early_alloc_top) {
                TRACE("memory: \t%016lx-%016lx: used by early allocator", DIRECT_TO_PHYS(start), DIRECT_TO_PHYS(early_alloc_top) - 1);
                start = early_alloc_top;
            }

            // and we can free it now
            TRACE("memory: \t%016lx-%016lx: free", DIRECT_TO_PHYS(start), DIRECT_TO_PHYS(end) - 1);
            RETHROW(phys_add_memory(start, end));
        }
    }

cleanup:
    return err;
}

err_t reclaim_bootloader_memory(void) {
    err_t err = NO_ERROR;

    bool irq_state = irq_spinlock_acquire(&g_phys_map_lock);

    TRACE("memory: Reclaiming bootloader memory");
    while (true) {
        // search for the next entry to reclaim
        phys_map_entry_t* entry;
        phys_map_entry_t* to_reclaim = NULL;
        list_for_each_entry(entry, &g_phys_map, link) {
            if (entry->type == PHYS_MAP_BOOTLOADER_RECLAIMABLE) {
                to_reclaim = entry;
                break;
            }
        }
        if (to_reclaim == NULL) {
            break;
        }

        // remember the values, the struct might change once we
        // convert the physical memory region
        void* start = PHYS_TO_DIRECT(to_reclaim->start);
        void* end = PHYS_TO_DIRECT(to_reclaim->end + 1);
        TRACE("memory: \t%016lx-%016lx", DIRECT_TO_PHYS(start), DIRECT_TO_PHYS(end) - 1);

        // mark as ram
        phys_map_convert_locked(PHYS_MAP_RAM, to_reclaim->start, (to_reclaim->end + 1) - to_reclaim->start);

        // and now add the memory into the buddy
        RETHROW(phys_add_memory(start, end));
    }

cleanup:
    irq_spinlock_release(&g_phys_map_lock, irq_state);

    return err;
}
