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
    const char* filename;
    size_t line;
} spinlock_t;

#define SPINLOCK_INIT ((spinlock_t){ .lock = ATOMIC_FLAG_INIT })

#define spinlock_acquire(lock) spinlock_acquire_(lock, __FILE__, __LINE__)
static inline void spinlock_acquire_(spinlock_t* lock, const char* filename, size_t line) {
    while (atomic_flag_test_and_set_explicit(&lock->lock, memory_order_acquire)) {
        cpu_relax();
    }
    lock->filename = filename;
    lock->line = line;
}

static inline void spinlock_release(spinlock_t* lock) {
    lock->filename = nullptr;
    lock->line = 0;
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

#define irq_spinlock_acquire(lock) irq_spinlock_acquire_(lock, __FILE__, __LINE__)
static inline bool irq_spinlock_acquire_(irq_spinlock_t* lock, const char* filename, size_t line) {
    bool irq_state = irq_save();
    spinlock_acquire_(&lock->lock, filename, line);
    return irq_state;
}

static inline void irq_spinlock_release(irq_spinlock_t* lock, bool irq_state) {
    spinlock_release(&lock->lock);
    irq_restore(irq_state);
}
