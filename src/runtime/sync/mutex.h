#pragma once

#include <stdatomic.h>

#include "lib/defs.h"

#define MUTEX_LOCKED_BIT BIT0
#define MUTEX_PARKED_BIT BIT1

typedef struct mutex {
    /**
     * This atomic integer holds the current state of the mutex instance. Only the two lowest bits
     * are used. See `LOCKED_BIT` and `PARKED_BIT` for the bitmask for these bits.
     *
     * State table:
     *
     * PARKED_BIT | LOCKED_BIT | Description
     *     0      |     0      | The mutex is not locked, nor is anyone waiting for it.
     * -----------+------------+------------------------------------------------------------------
     *     0      |     1      | The mutex is locked by exactly one thread. No other thread is
     *            |            | waiting for it.
     * -----------+------------+------------------------------------------------------------------
     *     1      |     0      | The mutex is not locked. One or more thread is parked or about to
     *            |            | park. At least one of the parked threads are just about to be
     *            |            | unparked, or a thread heading for parking might abort the park.
     * -----------+------------+------------------------------------------------------------------
     *     1      |     1      | The mutex is locked by exactly one thread. One or more thread is
     *            |            | parked or about to park, waiting for the lock to become available.
     *            |            | In this state, PARKED_BIT is only ever cleared when a bucket lock
     *            |            | is held (i.e. in a parking_lot_core callback). This ensures that
     *            |            | we never end up in a situation where there are parked threads but
     *            |            | PARKED_BIT is not set (which would result in those threads
     *            |            | potentially never getting woken up).
     */
    atomic_uint_fast8_t state;
} mutex_t;

void mutex_lock_slow(mutex_t* mutex);
void mutex_unlock_slow(mutex_t* mutex);

static inline void mutex_lock(mutex_t* mutex) {
    uint8_t expected = 0;
    if (!atomic_compare_exchange_weak_explicit(
        &mutex->state,
        &expected, MUTEX_LOCKED_BIT,
        memory_order_acquire, memory_order_relaxed
    )) {
        mutex_lock_slow(mutex);
    }
}

static inline void mutex_unlock(mutex_t* mutex) {
    uint8_t exepected = MUTEX_LOCKED_BIT;
    if (atomic_compare_exchange_strong_explicit(
        &mutex->state,
        &exepected, 0,
        memory_order_release, memory_order_relaxed
    )) {
        return;
    }

    mutex_unlock_slow(mutex);
}
