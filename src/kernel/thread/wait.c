#include "wait.h"
#include <stdint.h>

#include "arch/intrin.h"
#include "lib/atomic.h"
#include "lib/list.h"
#include "sched.h"
#include "mem/virt.h"
#include "sync/spinlock.h"

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
    list_t queue;
} wait_queue_t;

typedef struct wait_queue_entry {
    /**
     * Link in the wait queue
     */
    list_entry_t link;

    /**
     * The thread that is waiting
     */
    thread_t* thread;

    /**
     * The key that the thread is waiting for
     */
    void* key;
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

static bool wait_queue_prepare(wait_queue_t* queue, wait_queue_entry_t* entry,
                               void* key, wait_key_size_t key_size,
                               uint64_t old) {
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
    list_add(&queue->queue, &entry->link);
    spinlock_release(&queue->lock);
    return true;
}

static void wait_queue_finish(wait_queue_t* queue, wait_queue_entry_t* entry) {
    spinlock_acquire(&queue->lock);
    if (entry->link.next != nullptr) {
        list_del(&entry->link);
    }
    spinlock_release(&queue->lock);
}

INIT_CODE void init_atomic_wait(void) {
    for (size_t i = 0; i < WAIT_HASH_SIZE; i++) {
        list_init(&m_wait_hash[i].queue);
    }
}

void atomic_wait(void* key, wait_key_size_t size, uint64_t old, uint64_t deadline) {
    thread_t* thread = get_current_thread();
    wait_queue_t* queue = get_wait_queue_for_key(key);

    // Start parking now. Any unpark requests that catch `state` at this value or later will
    // cause the `scheduler_schedule` below to return. Note that this value will be made
    // visible to potential notifiers by the wait queue's lock.
    atomic_store_relaxed(&thread->state, THREAD_STATE_PARKING);

    wait_queue_entry_t entry = {
        .thread = thread,
        .key = key,
    };

    const bool irq_state = irq_save();

    if (!wait_queue_prepare(queue, &entry, key, size, old)) {
        irq_restore(irq_state);
        return;
    }

    if (deadline == 0) {
        scheduler_schedule();
    } else {
        scheduler_schedule_deadline(deadline);
    }

    // remove ourselves from the wait queue
    wait_queue_finish(queue, &entry);

    irq_restore(irq_state);
}

size_t atomic_notify(void* key, size_t count) {
    wait_queue_t* queue = get_wait_queue_for_key(key);

    bool irq_state = irq_save();
    spinlock_acquire(&queue->lock);

    // iterate the loop to find all the keys that match
    size_t woken = 0;
    wait_queue_entry_t* entry = nullptr;
    wait_queue_entry_t* tmp = nullptr;
    list_for_each_entry_safe(entry, tmp, &queue->queue, link) {
        if (entry->key == key) {
            // remove from the list
            list_del(&entry->link);

            if (scheduler_try_unpark(entry->thread)) {
                woken++;
            }

            // check if we have woken up enough people
            if (count != 0 && count == woken) {
                break;
            }
        }
    }

    spinlock_release(&queue->lock);
    irq_restore(irq_state);

    return woken;
}
