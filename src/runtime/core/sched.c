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

static scheduler_t* scheduler_get(void) {
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

/**
 * Actually schedule a new thread
 */
static void scheduler_schedule(scheduler_t* scheduler) {
    // the current thread we are switching from, if its
    // not parked then add it back to the run queue
    thread_t* current = get_current_thread();
    if (!current->parked) {
        list_add_tail(&scheduler->run_queue, &current->run_queue_link);
    }

    // the new thread to run
    thread_t* new_thread = scheduler_select_thread(scheduler);

    // if the new thread is not the idle thread then setup
    // a new preemption timer for 10ms
    if (new_thread != scheduler->idle) {
        timer_set_timeout(&scheduler->timer, 10);
    }

    // only perform a switch if we actually have a new thread
    if (new_thread != current) {
        thread_switch(new_thread, current);
    }
}

/**
 * Called from the scheduler timer, just marks that
 * we need to preempt, the actual preemption is done
 * when the timer interrupt finishes running
 */
static void scheduler_tick(timer_t* timer) {
    scheduler_t* scheduler = containerof(timer, scheduler_t, timer);
    ASSERT(scheduler == scheduler_get());
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

    // tell the kernel about our entry thunk, it will ensure that
    // shadow stacks are properly set with this
    sys_early_set_thread_entry_thunk(thread_entry_thunk);

    for (int i = 0; i < g_cpu_count; i++) {
        scheduler_t* scheduler = &m_schedulers[i];
        scheduler->timer.callback = scheduler_tick;

        list_init(&scheduler->run_queue);

        scheduler->idle = thread_create(scheduler_idle_thread, nullptr, "idle-%d", i);
        ASSERT(scheduler->idle != nullptr);
        scheduler->idle->parked = true;
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
    thread_resume(m_schedulers[get_cpu_id()].idle);
}

void sched_queue(thread_t* thread) {
    bool irq_state = irq_save();
    scheduler_t* scheduler = &m_schedulers[get_cpu_id()];
    list_add(&scheduler->run_queue, &thread->run_queue_link);
    irq_restore(irq_state);
}

void sched_yield(void) {
    if (m_preempt_count == 0) {
        // we can preempt right now
        bool irq_state = irq_save();
        scheduler_schedule(scheduler_get());
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
        scheduler_schedule(scheduler_get());

        // we just got back from schedule, we don't
        // need more preemption
        m_want_preempt = false;
    }

    irq_restore(irq_state);
}
