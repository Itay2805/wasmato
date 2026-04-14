#include "timer.h"

#include "tsc.h"
#include "arch/apic.h"
#include "thread/sched.h"
#include "arch/intrin.h"
#include "lib/pcpu.h"
#include "sync/spinlock.h"

typedef struct timers_queue {
    /**
     * The root of timers
     */
    rb_root_cached_t root;

    /**
     * The timers lock, must be taken with irqs disabled
     */
    spinlock_t lock;

    /**
     * Are we currently dispatching timers, if we are
     * then we are not going to set the timer right away
     * for new timers
     */
    bool dispatching;
} timers_queue_t;

/**
 * The per-cpu timer
 */
static CPU_LOCAL timers_queue_t m_timer = {};

static __always_inline bool timer_less(rb_node_t* a, const rb_node_t* b) {
    timer_t* ta = containerof(a, timer_t, node);
    timer_t* tb = containerof(b, timer_t, node);
    return ta->deadline < tb->deadline;
}

static void arch_timer_set_deadline(uint64_t deadline) {
    // TODO: LAPIC timer support
    tsc_timer_set_deadline(deadline);
}

static void arch_timer_clear(void) {
    // TODO: LAPIC timer support
    tsc_timer_clear();
}

__attribute__((interrupt))
void timer_interrupt_handler(interrupt_frame_t* frame) {
    // disable preemption so we won't switch context
    // while running timers
    preempt_disable();

    // we know the interrupt happened, we can ack it
    lapic_eoi();

    // get the root for this core and lock it, we are
    // with interrupts disabled already
    timers_queue_t* timers = pcpu_get_pointer(&m_timer);
    spinlock_acquire(&timers->lock);
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
        spinlock_acquire(&timer->lock.lock);
        if (get_tsc() < timer->deadline) {
            spinlock_release(&timer->lock.lock);
            break;
        }

        // remove from the tree
        rb_erase_cached(&timer->node, &timers->root);
        timer->queue = nullptr;
        spinlock_release(&timer->lock.lock);

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
        spinlock_release(&timers->lock);
        irq_enable();
        timer->callback(timer);
        irq_disable();
        spinlock_acquire(&timers->lock);
    }

    // mark we finished dispatching and unlock
    timers->dispatching = false;
    spinlock_release(&timers->lock);

    // if we still have a timer object in here it means that this is the next
    // time we should run it, setup the timer
    if (timer != nullptr) {
        arch_timer_set_deadline(timer->deadline);
    } else {
        arch_timer_clear();
    }

    // we re-enable preemption (tho right now
    // the interrupts are disabled), this will
    // switch to a new thread if need be
    preempt_enable();
}

INIT_CODE void init_timers_per_core(void) {
    // initialize all the roots
    m_timer.root = RB_ROOT_CACHED;
    m_timer.dispatching = false;
}

void timer_set_deadline(timer_t* timer, uint64_t deadline) {
    // NOTE: we need to perform an irq save and not just preempt disable
    //       because we want to be able to set timers from interrupts.
    bool irq_state = irq_spinlock_acquire(&timer->lock);

    // if already inside of a queue then remove it
    if (timer->queue != nullptr) {
        timers_queue_t* timers = timer->queue;
        timer->queue = nullptr;

        spinlock_acquire(&timers->lock);
        rb_erase_cached(&timer->node, &timers->root);
        spinlock_release(&timers->lock);
    }

    timers_queue_t* timers = pcpu_get_pointer(&m_timer);
    spinlock_acquire(&timers->lock);

    // update the time
    timer->deadline = deadline;
    timer->queue = timers;

    // and add it again
    if (rb_add_cached(&timer->node, &timers->root, timer_less) != nullptr) {
        if (!timers->dispatching) {
            // if we are the new leftmost node then we are the next timer to arrive,
            // so set the deadline to us, otherwise the dispatcher will set the deadline
            arch_timer_set_deadline(timer->deadline);
        }
    }

    spinlock_release(&timers->lock);
    irq_spinlock_release(&timer->lock, irq_state);
}

void timer_cancel(timer_t* timer) {
    bool irq_state = irq_spinlock_acquire(&timer->lock);

    if (timer->queue != nullptr) {
        // remove from the timers queue
        timers_queue_t* timers = timer->queue;
        timer->queue = nullptr;

        spinlock_acquire(&timers->lock);

        timer_t* leftmost = rb_entry_safe(rb_erase_cached(&timer->node, &timers->root), timer_t, node);
        if (!timers->dispatching && timers == pcpu_get_pointer(&m_timer)) {
            // update the per-cpu timer, only do this if we are on the correct core, if we are not
            // the timer on the other core will simply jump too early (or spuriously), and we handle
            // that gracefully.
            if (leftmost != nullptr) {
                // we are the new leftmost timer
                arch_timer_set_deadline(leftmost->deadline);
            } else {
                // no more timers
                arch_timer_clear();
            }
        }
        spinlock_release(&timers->lock);
    }

    irq_spinlock_release(&timer->lock, irq_state);
}
