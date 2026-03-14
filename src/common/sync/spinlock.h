#pragma once

#include <stdatomic.h>
#include <stdbool.h>

#include "arch/intrin.h"
#include "lib/defs.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Simple spinlock
//
// The kernel never runs with interrupts enabled, so IRQ locks are not needed
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct spinlock {
    atomic_flag lock;
} spinlock_t;

#define SPINLOCK_INIT ((spinlock_t){ .lock = ATOMIC_FLAG_INIT })

static inline void spinlock_acquire(spinlock_t* lock) {
    while (atomic_flag_test_and_set_explicit(&lock->lock, memory_order_acquire)) {
        cpu_relax();
    }
}

static inline void spinlock_release(spinlock_t* lock) {
    atomic_flag_clear_explicit(&lock->lock, memory_order_release);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Simple spinlock
//
// The kernel never runs with interrupts enabled, so IRQ locks are not needed
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct irq_spinlock {
    spinlock_t lock;
} irq_spinlock_t;

#define IRQ_SPINLOCK_INIT ((irq_spinlock_t){ .lock = ATOMIC_FLAG_INIT })

static inline bool irq_spinlock_acquire(irq_spinlock_t* lock) {
    bool irq_state = irq_save();
    spinlock_acquire(&lock->lock);
    return irq_state;
}

static inline void irq_spinlock_release(irq_spinlock_t* lock, bool irq_state) {
    spinlock_release(&lock->lock);
    irq_restore(irq_state);
}
