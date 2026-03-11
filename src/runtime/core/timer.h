#pragma once

#include <stdint.h>

#include "lib/tsc.h"
#include "lib/rbtree/rbtree.h"

struct timer;

typedef void (*timer_cb_t)(struct timer* timer);

typedef struct timer {
    /**
     * The node in the timer tree
     */
    rb_node_t node;

    /**
     * The deadline of the timer
     */
    uint64_t deadline;

    /**
     * The callback for the timer
     */
    timer_cb_t callback;
} timer_t;

void init_timers(uint8_t timer_vector);

void timer_set(timer_t* timer);

static inline void timer_set_timeout(timer_t* timer, uint64_t ms) {
    timer->deadline = tsc_ms_deadline(ms);
    timer_set(timer);
}
