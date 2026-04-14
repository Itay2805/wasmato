#include "mutex.h"

#include "arch/intrin.h"
#include "lib/atomic.h"
#include "lib/syscall.h"

#define SPIN_LIMIT 40

void mutex_lock_slow(mutex_t* mutex, uint32_t cur_state) {
    for (size_t i = 0;
         i < SPIN_LIMIT && cur_state != MUTEX_STATE_LOCKED_CONTENDED; i++) {
        cpu_relax();

        // Don't issue more RfO requests until we think we might have a chance.
        cur_state = atomic_load_relaxed(&mutex->state);
        if (cur_state != MUTEX_STATE_UNLOCKED) {
            continue;
        }

        if (atomic_compare_exchange_weak_acquire_relaxed(
                &mutex->state, &cur_state, MUTEX_STATE_LOCKED)) {
            return;
        }
    }

    while (
        atomic_exchange_acquire(&mutex->state, MUTEX_STATE_LOCKED_CONTENDED) !=
        MUTEX_STATE_UNLOCKED) {
        sys_atomic_wait32(&mutex->state, MUTEX_STATE_LOCKED_CONTENDED, 0);
    }
}

void mutex_unlock_slow(mutex_t* mutex) {
    sys_atomic_notify(&mutex->state, 1);
}
