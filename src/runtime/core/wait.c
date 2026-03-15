#include "wait.h"

#include "sched.h"
#include "proc/thread.h"
#include "lib/atomic.h"
#include "lib/tsc.h"

typedef struct wait_queue {
    /**
     * The lock to protect the queue
     */
    irq_spinlock_t lock;

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

static void wait_queue_prepare(wait_queue_t* queue, wait_queue_entry_t* entry) {
    spinlock_acquire(&queue->lock.lock);
    if (entry->link.next == nullptr) {
        list_add(&queue->queue, &entry->link);
    }
    entry->thread->state = THREAD_STATE_PARKED;
    spinlock_release(&queue->lock.lock);
}

static void wait_queue_finish(wait_queue_t* queue, wait_queue_entry_t* entry) {
    spinlock_acquire(&queue->lock.lock);
    if (entry->link.next != nullptr) {
        list_del(&entry->link);
    }
    spinlock_release(&queue->lock.lock);
}

static wait_queue_t m_wait_queue = {
    .queue = LIST_INIT(&m_wait_queue.queue)
};

static wait_queue_t* get_wait_queue_for_key(void* key) {
    return &m_wait_queue;
}

static bool atomic_check_key_acquire(void* key, wait_key_size_t size, uint64_t old) {
    if (size == WAIT_KEY_UINT32) {
        return atomic_load_acquire((_Atomic(uint32_t)*)key) == (uint32_t)old;
    } else if (size == WAIT_KEY_UINT64) {
        return atomic_load_acquire((_Atomic(uint64_t)*)key) == old;
    } else {
        ASSERT(!"Invalid key size");
    }
}

static bool atomic_check_key_relaxed(void* key, wait_key_size_t size, uint64_t old) {
    if (size == WAIT_KEY_UINT32) {
        return atomic_load_relaxed((_Atomic(uint32_t)*)key) == (uint32_t)old;
    } else if (size == WAIT_KEY_UINT64) {
        return atomic_load_relaxed((_Atomic(uint64_t)*)key) == old;
    } else {
        ASSERT(!"Invalid key size");
    }
}

bool atomic_wait(void* key, wait_key_size_t size, uint64_t old, uint64_t deadline) {
    thread_t* thread = get_current_thread();
    wait_queue_t* queue = get_wait_queue_for_key(key);

    // perform a quick check before we start to go to sleep
    if (!atomic_check_key_acquire(key, size, old)) {
        return true;
    }

    // if we are over the deadline return right away
    if (deadline != 0 && tsc_check_deadline(deadline)) {
        return false;
    }

    // prepare a wait queue entry, we give it
    // a ref to our thread
    wait_queue_entry_t entry = {
        .thread = thread_get(thread),
        .key = key,
    };

    // we are going to make changes to the thread so take a thread lock
    const bool irq_state = irq_spinlock_acquire(&thread->lock);

    // put ourselves in the wait-queue, so once we
    // go to sleep we can be woken up
    wait_queue_prepare(queue, &entry);

    bool timeout = false;
    do {
        // if we are over the deadline exit
        if (deadline != 0 && tsc_check_deadline(deadline)) {
            timeout = true;
            break;
        }

        // quick check that its still the
        // same value before we go to sleep
        if (atomic_check_key_relaxed(key, size, old)) {
            thread->state = THREAD_STATE_PARKED;
            if (deadline == 0) {
                scheduler_schedule();
            } else {
                scheduler_schedule_deadline(deadline);
            }
        }
    } while (atomic_check_key_acquire(key, size, old));

    // remove ourselves from the wait queue
    wait_queue_finish(queue, &entry);

    // we can safely unlock the thread
    irq_spinlock_release(&thread->lock, irq_state);

    // we can remove the ref to our thread now
    thread_put(entry.thread);

    return timeout;
}

size_t atomic_notify(void* key, size_t count) {
    wait_queue_t* queue = get_wait_queue_for_key(key);

    bool irq_state = irq_spinlock_acquire(&queue->lock);

    // iterate the loop to find all the keys that match
    size_t woken = 0;
    wait_queue_entry_t* entry = nullptr;
    wait_queue_entry_t* tmp = nullptr;
    list_for_each_entry_safe(entry, tmp, &queue->queue, link) {
        if (entry->key == key) {
            // remove from the list
            list_del(&entry->link);

            // just wake it up, we don't want
            // to complicate the cleanup, so just
            // add a ref to it now that we give it
            // back to the scheduler
            spinlock_acquire(&entry->thread->lock.lock);
            scheduler_queue(thread_get(entry->thread));
            woken++;
            spinlock_release(&entry->thread->lock.lock);

            // check if we have woken up enough people
            if (count != 0 && count == woken) {
                break;
            }
        }
    }

    irq_spinlock_release(&queue->lock, irq_state);

    return woken;
}
