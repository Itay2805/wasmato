#include "timer.h"

#include "sched.h"
#include "alloc/alloc.h"
#include "arch/intrin.h"
#include "lib/assert.h"
#include "sync/spinlock.h"
#include "uapi/syscall.h"

typedef struct timers {
    /**
     * The root of timers
     */
    rb_root_cached_t root;

    /**
     * Are we currently dispatching timers, if we are
     * then we are not going to set the timer right away
     * for new timers
     */
    bool dispatching;
} timers_t;

/**
 * The per-cpu timers
 */
static timers_t* m_timers = nullptr;

static bool timer_less(rb_node_t* a, const rb_node_t* b) {
    timer_t* ta = containerof(a, timer_t, node);
    timer_t* tb = containerof(b, timer_t, node);
    return ta->deadline < tb->deadline;
}

__attribute__((interrupt))
static void timer_interrupt_handler(interrupt_frame_t* frame) {
    // disable preemption so we won't switch context
    // while running timers
    preempt_disable();

    // we know the interrupt happened, we can ack it
    sys_interrupt_ack();

    // get the root for this core and lock them
    timers_t* timers = &m_timers[get_cpu_id()];
    timers->dispatching = true;

    // go over the timers in the tree that should be executed right now
    timer_t* timer = nullptr;
    for (;;) {
        rb_node_t* node = rb_first_cached(&timers->root);
        if (node == nullptr) {
            timer = nullptr;
            break;
        }

        timer = containerof(node, timer_t, node);
        if (get_tsc() < timer->deadline) {
            break;
        }

        // remove from the tree
        rb_erase_cached(&timer->node, &timers->root);
        timer->set = false;

        //
        // call the callback, this may modify the tree however it wants
        // to and even have an earlier timer because we will just iterate
        // again and get the first one again
        //
        // we also enable interrupts so more interrupts can run while we
        // are in the timer dispatch code
        //
        // our code ensures that we don't retrigger the timer from
        // setting the timer inside of it
        //
        irq_enable();
        timer->callback(timer);
        irq_disable();
    }

    // mark we finished dispatching and unlock
    timers->dispatching = false;

    // if we still have a timer object in here it means that this is the next
    // time we should run it, setup the timer
    if (timer != nullptr) {
        sys_timer_set_deadline(timer->deadline);
    } else {
        sys_timer_clear();
    }

    // we re-enable preemption (tho right now
    // the interrupts are disabled), this will
    // switch to a new thread if need be
    preempt_enable();
}

void init_timers(uint8_t timer_vector) {
    // allocate a root per cpu
    m_timers = mem_alloc(sizeof(*m_timers) * g_cpu_count);
    ASSERT(m_timers != nullptr);

    // initialize all the roots
    for (int i = 0; i < g_cpu_count; i++) {
        m_timers[i].root = RB_ROOT_CACHED;
        m_timers[i].dispatching = false;
    }

    // register the timer interrupt
    sys_early_interrupt_set_handler(timer_vector, timer_interrupt_handler);
}

void timer_set_deadline(timer_t* timer, uint64_t deadline) {
    // NOTE: we need to perform an irq save and not just preempt disable
    //       because we want to be able to set timers from interrupts.
    bool irq_state = irq_save();

    timers_t* timers = &m_timers[get_cpu_id()];

    // if already set remove it
    if (timer->set) {
        rb_erase_cached(&timer->node, &timers->root);
    }

    // update the time
    timer->deadline = deadline;
    timer->set = true;

    // and add it again
    if (rb_add_cached(&timer->node, &timers->root, timer_less) != nullptr) {
        if (!timers->dispatching) {
            // if we are the new leftmost node then we are the next timer to arrive,
            // so set the deadline to us, otherwise the dispatcher will set the deadline
            sys_timer_set_deadline(timer->deadline);
        }
    }

    irq_restore(irq_state);
}

void timer_cancel(timer_t* timer) {
    bool irq_state = irq_save();

    if (timer->set) {
        timers_t* timers = &m_timers[get_cpu_id()];

        timer_t* leftmost = rb_entry_safe(rb_erase_cached(&timer->node, &timers->root), timer_t, node);
        if (!timers->dispatching) {
            // update the per-cpu timer if in kernel
            if (leftmost != nullptr) {
                // new leftmost timer
                sys_timer_set_deadline(leftmost->deadline);
            } else {
                // no more timers
                sys_timer_clear();
            }
        }

        // no longer set
        timer->set = false;
    }

    irq_restore(irq_state);
}
