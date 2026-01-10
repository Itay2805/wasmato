#include "scheduler.h"

#include "pcpu.h"
#include "sync/spinlock.h"
#include "time/timer.h"
#include "time/tsc.h"

typedef struct core_scheduler_context {
    // the current thread
    thread_t* current;

    // the thread used when there is nothing else to run
    // it is never queued into the run queue
    thread_t* idle_thread;

    // the run queue and its lock
    list_t run_queue;
    irq_spinlock_t run_queue_lock;

    // timer used to reschedule
    timer_t timer;

    // when set to true preemption should not switch the context
    // but should set the want preemption flag instead
    int64_t preempt_count;

    // we got an preemption request while preempt count was 0
    // next time we enable preemption make sure to preempt
    bool want_reschedule;
} core_scheduler_context_t;

/**
 * The current cpu's context
 */
static CPU_LOCAL core_scheduler_context_t m_core = {};

err_t init_scheduler(void) {
    err_t err = NO_ERROR;

    // TODO: initialization in here

cleanup:
    return err;
}

static void scheduler_idle_loop(void* arg) {
    while (true) {
        asm volatile("hlt" ::: "memory");
    }
}

err_t scheduler_init_per_core(void) {
    err_t err = NO_ERROR;

    // setup the idle thread of this core
    thread_t* idle_thread = NULL;
    RETHROW(thread_create(&idle_thread, scheduler_idle_loop, NULL, "idle-%d", get_cpu_id()));
    m_core.idle_thread = idle_thread;

    // setup the run queue
    core_scheduler_context_t* core = pcpu_get_pointer(&m_core);
    list_init(&core->run_queue);
    m_core.run_queue_lock = IRQ_SPINLOCK_INIT;

    // we don't have anything right now
    m_core.current = NULL;

    // we start with a non-zero preempt count just
    // to ensure nothing weird happens
    m_core.preempt_count = 1;

cleanup:
    return err;
}

thread_t* scheduler_get_current_thread(void) {
    return m_core.current;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual scheduler
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * This is called when the timer fires,
 * just ask for a reschedule
 */
static void scheduler_timer_tick(timer_t* timer) {
    scheduler_reschedule();
}

/**
 * Configure the scheduler interrupt, for now a 10ms tick
 */
static void scheduler_reset_timer(void) {
    timer_set(pcpu_get_pointer(&m_core.timer), scheduler_timer_tick, tsc_ms_deadline(10));
}

/**
 * Actually switch from the current thread into the given thread, requeue the current
 * thread if need be
 */
static void scheduler_switch_thread(thread_t* thread, bool requeue) {
    // mark that we don't want to reschedule, since we
    // just executed something
    m_core.want_reschedule = false;

    // zero out the preemption count, so that the thread
    // can preempt
    m_core.preempt_count = 0;

    // set ourselves as the currently running thread
    thread_t* previous = m_core.current;
    m_core.current = thread;

    // add the previous thread to the run queue (only if it wasn't the idle thread)
    if (requeue && previous != m_core.idle_thread) {
        core_scheduler_context_t* core = pcpu_get_pointer(&m_core);
        bool irq_state = irq_spinlock_acquire(&core->run_queue_lock);
        list_add_tail(&core->run_queue, &previous->scheduler_node);
        irq_spinlock_release(&core->run_queue_lock, irq_state);
    }

    // TODO: setup the kernel stack for this thread to use

    // set the timeslice for the thread
    scheduler_reset_timer();

    if (previous == NULL || previous == m_core.idle_thread) {
        // we are scheduling away from the idle thread,
        // no need to save its context, just jump into it
        thread_jump(thread);
    } else {
        // and jump back into the thread, this will also properly
        // enable interrupts for the thread
        thread_switch(previous, thread);
    }
}

/**
 * Attempt to schedule a new thread, if false is returned then
 * there is no other thread to run and we should continue to run
 * the current thread
 */
static bool scheduler_schedule(bool requeue) {
    // we should be non-preemptible in here
    ASSERT(m_core.preempt_count == 1);

    core_scheduler_context_t* core = pcpu_get_pointer(&m_core);

    // cancel the schedule timer, we don't want it to interrupt
    // us at this point
    timer_cancel(&core->timer);

    // take an item from the queue (if any)
    bool irq_state = irq_spinlock_acquire(&core->run_queue_lock);
    list_entry_t* next = list_pop(&core->run_queue);
    irq_spinlock_release(&core->run_queue_lock, irq_state);

    // check what thread we should run
    thread_t* next_thread = NULL;
    if (next != NULL) {
        // we found some other thread to run, run it
        next_thread = containerof(next, thread_t, scheduler_node);

    } else if (requeue) {
        // we have no other thread to run, just continue
        // with the current thread
        scheduler_reset_timer();
        return false;

    } else {
        // we have no thread to run, jump into the
        // idle thread
        next_thread = m_core.idle_thread;
        thread_reset(next_thread);
    }

    // and switch to the thread, when we return from here
    // the preemption should be enabled
    scheduler_switch_thread(next_thread, requeue);

    // we got back
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scheduler API
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void scheduler_wakeup_thread(thread_t* thread) {
    // we are going to disable preemption to ensure that we don't get reshceduled
    // twice from this (with a race between releasing the irq lock and calling
    // the reschedule)
    scheduler_preempt_disable();

    // queue it properly
    core_scheduler_context_t* core = pcpu_get_pointer(&m_core);
    bool irq_state = irq_spinlock_acquire(&core->run_queue_lock);
    list_add(&core->run_queue, &thread->scheduler_node);
    irq_spinlock_release(&core->run_queue_lock, irq_state);

    // perform a reschedule, to allow the new thread to run
    scheduler_reschedule();

    // we can enable preemption again, if it wasn't already disable
    // this will reschedule now
    scheduler_preempt_enable();
}

void scheduler_park(scheduler_park_callback_t callback, void* arg) {
    // should not have a preempt count when going to sleep
    ASSERT(m_core.preempt_count == 0);

    // disable preemption so the scheduler won't hurt us
    scheduler_preempt_disable();

    if (callback != NULL && !callback(arg)) {
        // just go back
        scheduler_preempt_enable();
        return;
    }

    // we are going to sleep now,
    ASSERT(scheduler_schedule(false));
}

static bool scheduler_exit_callback(void* arg) {
    // remove from the current to ensure that nothing
    // can access the thread struct
    thread_t* current = m_core.current;
    m_core.current = NULL;

    // and free it completely
    thread_free(current);
    return true;
}

void scheduler_exit(void) {
    // to exit from the thread we are going to park
    // it and in the park callback free it
    scheduler_park(scheduler_exit_callback, NULL);
}

void scheduler_reschedule(void) {
    // check if we can even reschedule
    if (m_core.preempt_count != 0) {
        // mark that we need to reschedule
        m_core.want_reschedule = true;
        return;
    }

    // ensure we have a current thread
    ASSERT(m_core.current != NULL);

    // disable preemption and attempt to reschedule, if we failed
    // we are going to enable preemption again
    scheduler_preempt_disable();
    if (!scheduler_schedule(true)) {
        scheduler_preempt_enable();
    }
}

void scheduler_start_per_core(void) {
    // we should have a non-zero preempt count in here
    ASSERT(m_core.preempt_count == 1);

    // force enable interrupts at this point
    irq_enable();

    // jump into the scheduler and schedule, we don't need the
    // context we were at anymore
    ASSERT(!scheduler_schedule(false));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Preemption handling
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void scheduler_preempt_disable(void) {
    m_core.preempt_count++;
}

void scheduler_preempt_enable(void) {
    if (m_core.preempt_count == 1 && m_core.want_reschedule) {
        // if the schedule succeeds then we can just return since
        // the preempt count will now be zero
        if (scheduler_schedule(true)) {
            return;
        }

        // we no longer want to reschedule
        m_core.want_reschedule = false;
    }

    // enable preemption manually
    --m_core.preempt_count;
    ASSERT(m_core.preempt_count >= 0);
}

bool scheduler_is_preempt_disabled(void) {
    return m_core.preempt_count != 0;
}
