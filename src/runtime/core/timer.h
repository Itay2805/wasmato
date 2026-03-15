#pragma once

#include <stdint.h>

#include "lib/tsc.h"
#include "lib/rbtree/rbtree.h"
#include "sync/mutex.h"
#include "sync/spinlock.h"

typedef struct timer timer_t;
typedef struct timers_queue timers_queue_t;

typedef void (*timer_cb_t)(timer_t* timer);

struct timer {
    /**
     * The node in the timer tree
     */
    rb_node_t node;

    /**
     * Lock to protect the timer
     */
    irq_spinlock_t lock;

    /**
     * The deadline of the timer
     */
    uint64_t deadline;

    /**
     * The callback for the timer
     */
    timer_cb_t callback;

    /**
     * The timers queue we are on right now
     */
    timers_queue_t* queue;
};

void init_timers(uint8_t timer_vector);

void timer_set_deadline(timer_t* timer, uint64_t deadline);

static inline void timer_set_timeout(timer_t* timer, uint64_t ms) {
    timer_set_deadline(timer, tsc_ms_deadline(ms));
}

void timer_cancel(timer_t* timer);
