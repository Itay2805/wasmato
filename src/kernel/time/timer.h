#pragma once

#include <stdint.h>

#include "arch/intr.h"
#include "lib/tsc.h"
#include "lib/rbtree/rbtree.h"
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

/**
 * The timer interrupt vector
 */
void timer_interrupt_handler(interrupt_frame_t* frame);

/**
 * Initialize the timer dispatching for this core
 */
INIT_CODE void init_timers_per_core(void);

/**
 * Set a deadline for this timer
 *
 * @param timer     [IN] The timer to set the deadline on
 * @param deadline  [IN] The deadline for the timer
 */
void timer_set_deadline(timer_t* timer, uint64_t deadline);

/**
 * Set a timer with the given timeout in milliseconds
 *
 * @param timer     [IN] The timer to set the timeout on
 * @param ms        [IN] The timeout in milliseconds
 */
static inline void timer_set_timeout(timer_t* timer, uint64_t ms) {
    timer_set_deadline(timer, tsc_ms_deadline(ms));
}

/**
 * Cancel a timer, works across cores
 *
 * @param timer     [IN] The timer to cancel
 */
void timer_cancel(timer_t* timer);
