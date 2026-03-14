#include "sched.h"

#include <cpuid.h>

#include "thread.h"
#include "timer.h"
#include "alloc/alloc.h"
#include "arch/cpuid.h"
#include "arch/intrin.h"
#include "lib/except.h"
#include "proc/thread.h"
#include "uapi/syscall.h"

uint32_t g_cpu_count = 0;

typedef enum after_schedule_op {
    /**
     * No operation to perform after scheduling
     */
    AFTER_SCHEDULE_NOP,

    /**
     * Put the thread after scheduling
     */
    AFTER_SCHEDULE_PUT_THREAD,

    /**
     * Unlock the thread lock after scheduling
     */
    AFTER_SCHEDULE_UNLOCK_THREAD,
} after_schedule_op_t;

typedef struct scheduler_context {
    /**
     * The timer for the shceduler tick on the core
     */
    timer_t timer;

    /**
     * The queue of threads to run
     */
    list_t run_queue;

    /**
     * The idle thread of the core
     */
    thread_t* idle;

    /**
     * Operation to perform on the scheduler once it
     * runs the new thread
     */
    after_schedule_op_t after_schedule_op;

    /**
     * The argument for the next schedule op
     */
    thread_t* after_schedule_arg;
} scheduler_t;

/**
 * The schedulers for all the cores
 */
static scheduler_t* m_schedulers = nullptr;

/**
 * The current thread
 */
static thread_local thread_t* m_current = nullptr;

/**
 * Preemption is disabled if non-zero, this can be used
 * to quickly pin a thread for a while to do stuff that
 * should not preempt.
 *
 * Its a thread local to make it easier to modify without
 * needing to disable interrupts or anything alike because
 * we don't have per-cpu variables that we can access
 * atomically.
 */
static thread_local int32_t m_preempt_count = 0;

/**
 * Do we need to preempt whe restoring preempt count to zero?
 */
static thread_local bool m_want_preempt = false;

/**
 * Should we use umwait or not
 */
static bool m_umwait_supported = false;

thread_t* get_current_thread(void) {
    return m_current;
}

static scheduler_t* get_scheduler(void) {
    return &m_schedulers[get_cpu_id()];
}

static thread_t* scheduler_select_thread(scheduler_t* scheduler) {
    if (list_is_empty(&scheduler->run_queue)) {
        return scheduler->idle;
    }

    thread_t* thread = list_first_entry(&scheduler->run_queue, thread_t, run_queue_link);
    list_del(&thread->run_queue_link);
    return thread;
}

static void scheduler_after_schedule(void) {
    scheduler_t* scheduler = get_scheduler();
    switch (scheduler->after_schedule_op) {
        case AFTER_SCHEDULE_NOP: {
            // nothing to do
        } break;

        case AFTER_SCHEDULE_PUT_THREAD: {
            // unlock and put the thread
            spinlock_release(&scheduler->after_schedule_arg->lock.lock);
            thread_put(scheduler->after_schedule_arg);
        } break;

        case AFTER_SCHEDULE_UNLOCK_THREAD: {
            // unlock the thread, so it can be used and scheduled once again
            spinlock_release(&scheduler->after_schedule_arg->lock.lock);
        } break;

        default: {
            ASSERT(!"Invalid after schedule op");
        } break;
    }

    // clear the state
    scheduler->after_schedule_op = AFTER_SCHEDULE_NOP;
    scheduler->after_schedule_arg = nullptr;
}

/**
 * Actually schedule a new thread
 * Runs with interrupts disabled for the entire time.
 */
static void scheduler_schedule(scheduler_t* scheduler) {
    thread_t* current = get_current_thread();

    if (current == scheduler->idle) {
        // idle threads don't need anything special because they can't
        // be readied or anything alike, so we don't even bother
        // locking them
        scheduler->after_schedule_op = AFTER_SCHEDULE_NOP;

        // can only be running, can't be parked or dead
        ASSERT(current->state == THREAD_STATE_RUNNING);
        current->state = THREAD_STATE_READY;
    } else {
        // we lock the current to ensure no wake-ups happen
        // while we are parking it
        // NOTE: we are running with interrupts disabled in here, so we
        //       don't need to save the irq
        spinlock_acquire(&current->lock.lock);

        if (current->state == THREAD_STATE_DEAD || current->state == THREAD_STATE_PARKED) {
            // we need to unref the thread, it will possibly free it
            // so we need to do it after a stack switch
            scheduler->after_schedule_op = AFTER_SCHEDULE_PUT_THREAD;
            scheduler->after_schedule_arg = current;

        } else if (current->state == THREAD_STATE_RUNNING) {
            scheduler->after_schedule_op = AFTER_SCHEDULE_UNLOCK_THREAD;
            scheduler->after_schedule_arg = current;

            // place back on the run queue and set the state as ready
            current->state = THREAD_STATE_READY;
            list_add_tail(&scheduler->run_queue, &current->run_queue_link);

        } else {
            ASSERT(!"Invalid thread state");
        }
    }

    // the new thread to run, the state must be ready, which prevents
    // wakeups from touching anything on the thread
    thread_t* new_thread = scheduler_select_thread(scheduler);
    spinlock_acquire(&new_thread->lock.lock);
    ASSERT(new_thread->state == THREAD_STATE_READY);
    new_thread->state = THREAD_STATE_RUNNING;
    spinlock_release(&new_thread->lock.lock);

    // if the new thread is not the idle thread then setup
    // a new preemption timer for 10ms
    if (new_thread != scheduler->idle) {
        timer_set_timeout(&scheduler->timer, 10);
    } else {
        timer_cancel(&scheduler->timer);
    }

    // only perform a switch if we actually have a new thread
    if (new_thread != current) {
        thread_switch(new_thread, current);
    }

    // cleanup after the schedule, this will unlock
    // anything that needs to be unlocked
    scheduler_after_schedule();
}

/**
 * Called from the scheduler timer, just marks that
 * we need to preempt, the actual preemption is done
 * when the timer interrupt finishes running
 */
static void scheduler_tick(timer_t* timer) {
    scheduler_t* scheduler = containerof(timer, scheduler_t, timer);
    ASSERT(scheduler == get_scheduler());
    ASSERT(m_preempt_count > 0);
    m_want_preempt = true;
}

static void umonitor_wait(_Atomic(uint32_t)* addr, uint32_t expected) {
    for (;;) {
        uint32_t value = atomic_load_explicit(addr, memory_order_acquire);

        if (value != expected) {
            return;
        }

        _umonitor(addr);

        value = atomic_load_explicit(addr, memory_order_acquire);
        if (value != expected) {
            return;
        }

        // BIT1 == break on interrupt even with IF=0
        // TODO: disable timer before entering the wait
        // loop and do it manually?
        _umwait(0, UINT64_MAX);
    }
}

static void scheduler_idle_thread(void* arg) {
    //
    // The first thing we need to do is to preempt
    // the current idle thread to actually let the
    // scheduler start scheduling
    //
    sched_yield();

    //
    // After that first yield we can just while-true
    // go to sleep
    //
    while (true) {
        _Atomic(uint32_t) arg = 0;

        if (m_umwait_supported) {
            umonitor_wait(&arg, 0);
        } else {
            sys_monitor_wait(&arg, 0);
        }

        // TODO: if someone woke us up then yield, otherwise
        //       the interrupt handler did its work and we can
        //       go back to sleep
    }
}

void thread_entry_point(thread_t* thread, thread_entry_t entry, void* arg) {
    // set the current thread-local, and only after it enable
    // interrupts to ensure we won't get a preemption beforehand
    m_current = thread;
    irq_enable();

    // call the entry point
    entry(arg);

    // if it returns just exit
    ASSERT(!"TODO: call exit");
}

void init_sched(void) {
    m_schedulers = mem_alloc(sizeof(*m_schedulers) * g_cpu_count);
    ASSERT(m_schedulers != nullptr);
    memset(m_schedulers, 0, sizeof(*m_schedulers) * g_cpu_count);

    // tell the kernel about our entry thunk, it will ensure that
    // shadow stacks are properly set with this
    sys_early_set_thread_entry_thunk(thread_entry_thunk);

    for (int i = 0; i < g_cpu_count; i++) {
        scheduler_t* scheduler = &m_schedulers[i];
        scheduler->timer.callback = scheduler_tick;

        list_init(&scheduler->run_queue);

        scheduler->idle = thread_create(scheduler_idle_thread, nullptr, "idle-%d", i);
        ASSERT(scheduler->idle != nullptr);
    }

    // check for umwait support and use that whenever supported
    // because it offers better perf
    uint32_t a, b, d;
    CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS_ECX structured_extended_feature_flags_ecx = {};
    ASSERT(__get_cpuid_count(
        CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS,
        CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS_SUB_LEAF_INFO,
        &a,
        &b,
        &structured_extended_feature_flags_ecx.raw,
        &d
    ));
    if (structured_extended_feature_flags_ecx.WAITPKG) {
        TRACE("sched: using umwait in idle thread");
        m_umwait_supported = true;
    } else {
        TRACE("sched: using kernel monitor in idle thread");
    }
}

void sched_start_per_core(void) {
    // start by running the idle threads, just to get into a stable
    // stack, from there we will do the rest
    thread_t* thread = get_scheduler()->idle;
    thread->state = THREAD_STATE_RUNNING;
    thread_resume(thread);
}

void sched_ready_thread(thread_t* thread) {
    // TODO: check the thread state outside of the lock? just to prevent the lock

    const bool irq_state = irq_spinlock_acquire(&thread->lock);

    ASSERT(
        thread->state == THREAD_STATE_RUNNING ||
        thread->state == THREAD_STATE_PARKED ||
        thread->state == THREAD_STATE_READY
    );

    // if the thread is parked then ready it
    if (thread->state == THREAD_STATE_PARKED) {
        list_add(&get_scheduler()->run_queue, &thread->run_queue_link);
        thread->state = THREAD_STATE_READY;
    }

    irq_spinlock_release(&thread->lock, irq_state);
}

void sched_yield(void) {
    if (m_preempt_count == 0) {
        // we can preempt right now
        bool irq_state = irq_save();
        scheduler_schedule(get_scheduler());
        irq_restore(irq_state);
    } else {
        // there is preemption right now
        m_want_preempt = true;
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Preemption handling
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void preempt_disable(void) {
    ASSERT(m_preempt_count >= 0);
    m_preempt_count++;
}

void preempt_enable(void) {
    ASSERT(m_preempt_count > 0);

    if (m_preempt_count > 1) {
        // just decrement normally and return, we
        // can't preempt yet
        --m_preempt_count;
        return;
    }

    // we are going to disable interrupts to make
    // sure nothing interrupts us while we are in
    // the scheduler itself
    bool irq_state = irq_save();

    // reduce the preempt count
    // and check if we need to
    // preempt right now
    m_preempt_count--;

    if (m_want_preempt) {
        // reschedule, we are already running
        // without interrupts so this can just
        // run
        scheduler_schedule(get_scheduler());

        // we just got back from schedule, we don't
        // need more preemption
        m_want_preempt = false;
    }

    irq_restore(irq_state);
}
