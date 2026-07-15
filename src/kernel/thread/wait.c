#include "wait.h"
#include <stdint.h>

#include "arch/intrin.h"
#include "lib/assert.h"
#include "lib/atomic.h"
#include "lib/defs.h"
#include "lib/except.h"
#include "lib/list.h"
#include "lib/log.h"
#include "lib/tsc.h"
#include "mem/mappings.h"
#include "mem/phys.h"
#include "mem/vmar.h"
#include "sched.h"
#include "mem/virt.h"
#include "sync/spinlock.h"
#include "uapi/page.h"
#include "uapi/wait.h"
#include "user/syscall.h"

#define WAIT_HASH_SHIFT 8
#define WAIT_HASH_SIZE (1 << WAIT_HASH_SHIFT)

#define GOLDEN_RATIO_64 0x61C8864680B583EBull

typedef struct wait_queue {
    /**
     * The lock to protect the queue
     */
    spinlock_t lock;

    /**
     * The queue itself
     */
    struct wait_queue_entry* head;
} wait_queue_t;

typedef struct wait_queue_entry {
    /**
     * Link in the wait queue
     */
    struct wait_queue_entry* next;

    /**
     * The thread that is waiting
     */
    thread_t* thread;

    /**
     * The key that the thread is waiting for
     */
    void* key;

    /** 
     * Mask to apply on the key when checking it
     */
    uint64_t mask;
} wait_queue_entry_t;

static wait_queue_t m_wait_hash[WAIT_HASH_SIZE];

static wait_queue_t* get_wait_queue_for_key(void* key) {
    size_t hash = ((uint64_t)key * GOLDEN_RATIO_64) >> (64 - WAIT_HASH_SHIFT);
    return &m_wait_hash[hash];
}

static bool atomic_check_user_key(void* key, wait_key_size_t size, uint64_t old) {
    bool result = false;

    user_access_enable();
    if (size == WAIT_KEY_UINT32) {
        result = atomic_load_relaxed((_Atomic(uint32_t)*) key) == (uint32_t) old;
    } else if (size == WAIT_KEY_UINT64) {
        result = atomic_load_relaxed((_Atomic(uint64_t)*) key) == old;
    } else {
        ASSERT(!"Invalid key size");
    }
    user_access_disable();

    return result;
}

static bool wait_queue_prepare(
    wait_queue_entry_t* entry,
    wait_key_size_t key_size,
    uint64_t old
) {
    void* key = entry->key;
    wait_queue_t* queue = get_wait_queue_for_key(key);

    spinlock_acquire(&queue->lock);

    // Check whether we should really be going to sleep under the wait queue
    // lock. It's important to do this under the queue lock so we don't race
    // against concurrent notifications: anyone who has changed `key` and has
    // called `notify` will either observe us in the queue and wake us, or will
    // make their change visible to the check here when they release the lock.
    if (!atomic_check_user_key(key, key_size, old)) {
        spinlock_release(&queue->lock);
        return false;
    }

    entry->next = queue->head;
    queue->head = entry;
    spinlock_release(&queue->lock);
    return true;
}

static void wait_queue_finish(wait_queue_entry_t* remove) {
    wait_queue_t* queue = get_wait_queue_for_key(remove->key);

    spinlock_acquire(&queue->lock);

    wait_queue_entry_t** indirect = &queue->head;
    while (*indirect != nullptr) {
        wait_queue_entry_t* entry = *indirect;

        if (entry == remove) {
            *indirect = entry->next;
            break;
        } else {
            indirect = &((*indirect)->next);
        }
    }

    spinlock_release(&queue->lock);
}

wait_status_t atomic_wait(wait_entry_t* entries, size_t count, uint64_t deadline) {
    size_t entries_len;
    ASSERT(!__builtin_mul_overflow(sizeof(wait_entry_t), count, &entries_len));
    assert_user_range(entries, entries_len);

    thread_t* thread = get_current_thread();
    size_t queued = 0;
    wait_status_t status = WAIT_STATUS_SUCCESS;

    // Start parking now. Any unpark requests that catch `state` at this value or later will
    // cause the `scheduler_schedule` below to return. Note that this value will be made
    // visible to potential notifiers by the wait queue's lock.
    atomic_store_relaxed(&thread->state, THREAD_STATE_PARKING);

    // stack entries if small enough, otherwise allocate
    STATIC_ASSERT(sizeof(wait_entry_t) == sizeof(wait_queue_entry_t));
    wait_queue_entry_t* wait_entries = nullptr;
    bool is_phys_alloc = false;
    vmar_t* vmar = nullptr;

    wait_queue_entry_t stack_wait_entries[64];
    if (count <= ARRAY_LENGTH(stack_wait_entries)) {
        // the count is small enough to use the stack
        wait_entries = stack_wait_entries;

    } else if (entries_len <= PHYS_BUDDY_MAX_SIZE) {
        // the count might fit into a body allocation, 
        // try to do it first 
        wait_entries = phys_alloc(entries_len);
        is_phys_alloc = true;
    }
    
    if (wait_entries == nullptr) {
        // the count was too big and we failed to allocate from 
        // the body, allocate a VMAR instead so we can do a non
        // contig allocation
        vmar = vmar_allocate(&g_kernel_memory, SIZE_TO_PAGES(entries_len), nullptr);
        wait_entries = vmar->base;
    }

    if (wait_entries == nullptr) {
        // if we still failed to allocate just return 
        // out of memory
        return WAIT_STATUS_OUT_OF_MEMORY;
    }

    const bool irq_state = irq_save();

    for (queued = 0; queued < count; queued++) {
        // read the entry
        user_access_enable();
        wait_entry_t entry = entries[queued];
        user_access_disable();

        // make sure thek key is in usermode
        size_t key_size_bytes = entry.key_size == WAIT_KEY_UINT32 ? sizeof(uint32_t) : sizeof(uint64_t);
        assert_user_range(entry.key, key_size_bytes);

        // setup the entry
        wait_entries[queued] = (wait_queue_entry_t){
            .key = entry.key,
            .mask = entry.mask,
            .thread = thread
        };

        if (!wait_queue_prepare(&wait_entries[queued], entry.key_size, entry.old)) {
            // We are going to abort our parking, we need to go back to RUNNING.
            atomic_store_relaxed(&thread->state, THREAD_STATE_RUNNING);

            // the state was not-equal
            status = WAIT_STATUS_NOT_EQUAL;
            goto unpark;
        }
    }

    if (deadline == -1) {
        scheduler_schedule();
    } else {
        scheduler_schedule_deadline(deadline);
    }

unpark:
    // remove ourselves from all the wait queues
    for (size_t i = 0; i < queued; i++) {
        wait_queue_finish(&wait_entries[i]);
    }

    irq_restore(irq_state);

    // free it if we need to
    if (is_phys_alloc) {
        phys_free(wait_entries, entries_len);
    } else if (vmar != nullptr) {
        vmar_free(vmar);
    }

    return status;
}

size_t atomic_notify(void* key, uint64_t mask, size_t count) {
    wait_queue_t* queue = get_wait_queue_for_key(key);

    bool irq_state = irq_save();
    spinlock_acquire(&queue->lock);

    // iterate the loop to find all the keys that match
    size_t woken = 0;
    wait_queue_entry_t** indirect = &queue->head;
    while (*indirect != nullptr) {
        wait_queue_entry_t* entry = *indirect;

        if (entry->key == key && (entry->mask & mask)) {
            *indirect = entry->next;

            if (scheduler_try_unpark(entry->thread)) {
                woken++;
            }

            // check if we have woken up enough people
            if (count != 0 && count == woken) {
                break;
            }
        } else {
            indirect = &((*indirect)->next);
        }
    }

    spinlock_release(&queue->lock);
    irq_restore(irq_state);

    return woken;
}
