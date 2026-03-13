#include "sched.h"

#include "thread.h"
#include "timer.h"
#include "alloc/alloc.h"
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

    /**
     * The preempt count of the core
     */
    int preempt_count;

    /**
     * Do we need to preempt
     */
    bool want_preempt;
} scheduler_t;

/**
 * The schedulers for all the cores
 */
static scheduler_t* m_schedulers = nullptr;

/**
 * The current thread
 */
static thread_local thread_t* m_current = nullptr;

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
        timer_set_timeout(&scheduler->timer, tsc_ms_deadline(10));
    }

    // switch to the thread, this will return when
    // the curren thread resumes
    thread_switch(current, new_thread);
}

/**
 * Called from the scheduler timer, just marks that
 * we need to preempt, the actual preemption is done
 * when the timer interrupt finishes running
 */
static void scheduler_tick(timer_t* timer) {
    scheduler_t* scheduler = containerof(timer, scheduler_t, timer);
    ASSERT(scheduler == scheduler_get());
    ASSERT(scheduler->preempt_count > 0);
    scheduler->want_preempt = true;
}

static void scheduler_idle_thread(void* arg) {
    while (true) {
        _Atomic(uint32_t) arg = 0;
        sys_monitor_wait(&arg, 0);
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

    for (int i = 0; i < g_cpu_count; i++) {
        scheduler_t* scheduler = &m_schedulers[i];
        scheduler->timer.callback = scheduler_tick;

        list_init(&scheduler->run_queue);

        scheduler->idle = thread_create(scheduler_idle_thread, nullptr, "idle-%d", i);
        ASSERT(scheduler->idle != nullptr);
        scheduler->idle->parked = true;
    }
}

void sched_start_per_core(void) {
    // find the thread that we need to run and call it
    scheduler_t* scheduler = &m_schedulers[get_cpu_id()];
    thread_t* thread = scheduler_select_thread(scheduler);
    thread_resume(thread);
}

void sched_queue(thread_t* thread) {
    bool irq_state = irq_save();
    scheduler_t* scheduler = &m_schedulers[get_cpu_id()];
    list_add(&scheduler->run_queue, &thread->run_queue_link);
    irq_restore(irq_state);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Preemption handling
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void preempt_disable(void) {
    bool irq_state = irq_save();
    scheduler_t* sched = scheduler_get();
    sched->preempt_count++;
    irq_restore(irq_state);
}

void preempt_enable(void) {
    // can't preempt right now because preemption
    // is disabled, so can't switch cores
    scheduler_t* sched = scheduler_get();
    ASSERT(sched->preempt_count > 0);

    if (sched->preempt_count > 1) {
        // just decrement normally and return, we
        // can't preempt yet
        --sched->preempt_count;
        return;
    }

    // we are going to disable interrupts to make
    // sure nothing interrupts us while we are in
    // the scheduler itself
    bool irq_state = irq_save();

    // reduce the preempt count
    // and check if we need to
    // preempt right now
    sched->preempt_count--;

    if (sched->want_preempt) {
        // reschedule, we are already running
        // without interrupts so this can just
        // run
        scheduler_schedule(sched);

        // we just got back from schedule, we don't
        // need more preemption
        sched->want_preempt = false;
    }

    irq_restore(irq_state);
}
