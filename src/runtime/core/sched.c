#include "sched.h"

#include <cpuid.h>
#include <stdalign.h>

#include "thread.h"
#include "timer.h"
#include "alloc/alloc.h"
#include "arch/cpuid.h"
#include "arch/intrin.h"
#include "lib/atomic.h"
#include "lib/except.h"
#include "proc/thread.h"
#include "uapi/syscall.h"

uint32_t g_cpu_count = 0;

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
     * The last thread, we need to unlock it after
     * the context switch
     */
    thread_t* last_thread;

    /**
     * Is the core parked
     */
    _Atomic(uint32_t) parked;

    /**
     * Should we also release the thread since it was unparked
     */
    bool release_thread;
} __attribute__((aligned(128))) scheduler_t;

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

thread_t* get_current_thread(void) {
    return m_current;
}

static void scheduler_wakeup(scheduler_t* scheduler) {
    // store into the parked thing, this will ensure
    // the monitor wakes up
    atomic_store_release(&scheduler->parked, 0);
}

//----------------------------------------------------------------------------------------------------------------------
// Private API
//----------------------------------------------------------------------------------------------------------------------

/**
 * Should we use umwait or not
 */
static bool m_umwait_supported = false;

static scheduler_t* get_scheduler(void) {
    return &m_schedulers[get_cpu_id()];
}

static thread_t* scheduler_select_thread(scheduler_t* scheduler) {
    if (list_is_empty(&scheduler->run_queue)) {
        return scheduler->idle;
    }

    thread_t* thread = list_first_entry(&scheduler->run_queue, thread_t, link);
    list_del(&thread->link);
    return thread;
}

static void scheduler_after_schedule(void) {
    scheduler_t* scheduler = get_scheduler();

    // we are now on the new thread, but the last thread
    // that was on the core is still locked by us, release
    // the lock right now, we possibly also need to unref
    // the thread and let it get freed
    thread_t* last_thread = scheduler->last_thread;
    if (last_thread != nullptr) {
        scheduler->last_thread = nullptr;
        spinlock_release(&last_thread->lock.lock);
        if (scheduler->release_thread) {
            scheduler->release_thread = false;
            thread_put(last_thread);
        }
    }
}

/**
 * Actually schedule a new thread, the current thread should
 * be locked at this point.
 *
 * Runs with interrupts disabled for the entire time.
 */
void scheduler_schedule(void) {
    // must be with irqs disabled but with preemption enabled
    ASSERT(!is_irq_enabled());
    ASSERT(m_preempt_count == 0);

    scheduler_t* scheduler = get_scheduler();
    thread_t* current = get_current_thread();

    if (current->state == THREAD_STATE_DEAD || current->state == THREAD_STATE_PARKED) {
        // the thread has died or got parked, we are going to remove
        // the ref that the scheduler owns
        scheduler->release_thread = true;

    } else if (current->state == THREAD_STATE_RUNNING) {
        // place back on the run queue and set the state as ready
        current->state = THREAD_STATE_READY;

        // only put non-idle threads into the run queue
        if (current != scheduler->idle) {
            list_add_tail(&scheduler->run_queue, &current->link);
        }

    } else {
        ASSERT(!"Invalid thread state");
    }

    // remember the last thread
    // so we can unlock it
    scheduler->last_thread = current;

    // select a new thread
    thread_t* new_thread = scheduler_select_thread(scheduler);
    if (new_thread == current) {
        // mark it as running, we still hold its lock
        new_thread->state = THREAD_STATE_RUNNING;

        // the same thread was picked, we can just return
        // without doing anything
        scheduler->last_thread = nullptr;
        scheduler->release_thread = false;
        return;
    }

    // acquire the lock of the new thread and set
    // it as runnable
    spinlock_acquire(&new_thread->lock.lock);
    ASSERT(new_thread->state == THREAD_STATE_READY);
    new_thread->state = THREAD_STATE_RUNNING;

    // if the new thread is not the idle thread then setup
    // a new preemption timer for 10ms
    if (new_thread != scheduler->idle) {
        timer_set_timeout(&scheduler->timer, 10);
    } else {
        timer_cancel(&scheduler->timer);
    }

    // actually switch to the new thread
    thread_switch(new_thread, current);

    // cleanup after schedule
    scheduler_after_schedule();
}

typedef struct schedule_timer {
    timer_t timer;
    thread_t* thread;
} schedule_timer_t;

static void schedule_timer_wakeup(timer_t* timer) {
    schedule_timer_t* ctx = containerof(timer, schedule_timer_t, timer);
    thread_t* thread = ctx->thread;
    bool scheduled = false;

    // wake up the thread if its not running already
    bool irq_state = irq_spinlock_acquire(&thread->lock);
    if (thread->state == THREAD_STATE_PARKED) {
        scheduler_queue(thread);
        scheduled = true;
    }

    // we delete the ref to tell the other side that
    // they don't need to release the ref
    ctx->thread = nullptr;

    irq_spinlock_release(&thread->lock, irq_state);

    // if we didn't schedule release the thread, otherwise
    // we just passed it into the scheduler again
    if (!scheduled) {
        thread_put(thread);
    }
}

void scheduler_schedule_deadline(uint64_t deadline) {
    // if we are already over the timeout then
    // just return right away
    if (tsc_check_deadline(deadline)) {
        return;
    }

    // setup the timer, we give it its own ref of the thread
    // in case it dies before the timer fires
    thread_t* thread = get_current_thread();
    schedule_timer_t timer = {
        .timer = {
            .callback = schedule_timer_wakeup
        },
        .thread = thread_get(thread)
    };

    // setup the timer, if we still race then the interrupt
    // will just fire as soon as we are done
    timer_set_deadline(&timer.timer, deadline);

    // schedule the thread now
    thread->state = THREAD_STATE_PARKED;
    scheduler_schedule();

    // ensure the timer is disabled before we
    // check anything else
    timer_cancel(&timer.timer);

    if (timer.thread != nullptr) {
        // put the ref away
        thread_put(timer.thread);
    }
}

void scheduler_queue(thread_t* thread) {
    ASSERT(thread->state == THREAD_STATE_PARKED);
    ASSERT(!is_irq_enabled());
    scheduler_t* scheduler = get_scheduler();

    // place on the run queue
    thread->state = THREAD_STATE_READY;
    list_add_tail(&scheduler->run_queue, &thread->link);

    // ensure the scheduler is woken up
    scheduler_wakeup(scheduler);
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
        uint32_t value = atomic_load_acquire(addr);

        if (value != expected) {
            return;
        }

        _umonitor(addr);

        value = atomic_load_acquire(addr);
        if (value != expected) {
            return;
        }

        _umwait(0, UINT64_MAX);
    }
}

static void scheduler_idle_thread(void* arg) {
    scheduler_t* scheduler = arg;

    //
    // After that first yield we can just while-true
    // go to sleep
    //
    for (;;) {
        // start by yielding to ensure anyone who can
        // run will run
        sched_yield();

        // mark the core as parked, anyone who wants to wake us
        // up will set this to 0, which will cause us to exit
        // the monitor loop
        atomic_store_relaxed(&scheduler->parked, 1);

        // use the monitor to actually wait on the cache line
        while (atomic_load_acquire(&scheduler->parked) != 0) {
            if (m_umwait_supported) {
                umonitor_wait(&scheduler->parked, 1);
            } else {
                sys_monitor_wait(&scheduler->parked, 1);
            }
        }
    }
}

void thread_entry_point(thread_t* thread, thread_entry_t entry, void* arg) {
    // set the current thread-local, so we can access it
    m_current = thread;

    // this is right after scheduling,
    // so cleanup
    scheduler_after_schedule();

    // now we are ready to enable interrupts
    irq_spinlock_release(&thread->lock, true);

    // call the entry point
    entry(arg);

    // if it returns jus exit
    thread_exit();
}

void init_sched(void) {
    m_schedulers = mem_alloc_aligned(sizeof(scheduler_t) * g_cpu_count, alignof(scheduler_t));
    ASSERT(m_schedulers != nullptr);
    memset(m_schedulers, 0, sizeof(*m_schedulers) * g_cpu_count);

    // tell the kernel about our entry thunk, it will ensure that
    // shadow stacks are properly set with this
    sys_early_set_thread_entry_thunk(thread_entry_thunk);

    for (int i = 0; i < g_cpu_count; i++) {
        scheduler_t* scheduler = &m_schedulers[i];
        scheduler->timer.callback = scheduler_tick;

        list_init(&scheduler->run_queue);

        scheduler->idle = thread_create(scheduler_idle_thread, scheduler, "idle-%d", i);
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

//----------------------------------------------------------------------------------------------------------------------
// Public API
//----------------------------------------------------------------------------------------------------------------------

void sched_yield(void) {
    if (m_preempt_count == 0) {
        // we can preempt right now
        thread_t* current = get_current_thread();
        const bool irq_state = irq_spinlock_acquire(&current->lock);
        scheduler_schedule();
        irq_spinlock_release(&current->lock, irq_state);
    } else {
        // there is preemption right now
        m_want_preempt = true;
    }
}

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
    thread_t* current = get_current_thread();
    const bool irq_state = irq_spinlock_acquire(&current->lock);

    // reduce the preempt count
    // and check if we need to
    // preempt right now
    m_preempt_count--;

    if (m_want_preempt) {
        // reschedule, we are already running
        // without interrupts so this can just
        // run
        scheduler_schedule();

        // we just got back from schedule, we don't
        // need more preemption
        m_want_preempt = false;
    }

    irq_spinlock_release(&current->lock, irq_state);
}
