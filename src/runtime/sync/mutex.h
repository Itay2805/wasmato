#pragma once

#include <stdint.h>

#include "lib/atomic.h"

enum {
    MUTEX_STATE_UNLOCKED = 0,
    MUTEX_STATE_LOCKED = 1,
    MUTEX_STATE_LOCKED_CONTENDED = 2,
};

typedef struct mutex {
    _Atomic uint32_t state;
} mutex_t;

void mutex_lock_slow(mutex_t* mutex, uint32_t cur_state);
void mutex_unlock_slow(mutex_t* mutex);

static inline void mutex_lock(mutex_t* mutex) {
    uint32_t expected = MUTEX_STATE_UNLOCKED;
    if (!atomic_compare_exchange_weak_acquire_relaxed(&mutex->state, &expected,
                                                      MUTEX_STATE_LOCKED)) {
        mutex_lock_slow(mutex, expected);
    }
}

static inline void mutex_unlock(mutex_t* mutex) {
    if (atomic_exchange_release(&mutex->state, MUTEX_STATE_UNLOCKED) ==
        MUTEX_STATE_LOCKED_CONTENDED) {
        mutex_unlock_slow(mutex);
    }
}
