#pragma once

#include <stdnoreturn.h>
#include <stdint.h>

#include "lib/except.h"
#include "thread.h"

/**
 * Is the mwait/monitor instruction supported and can it
 * be used to wait for interrupts
 */
extern LATE_RO bool g_monitor_supported;

INIT_CODE void init_sched_per_core(void);

INIT_CODE noreturn void sched_start_per_core(void);

//----------------------------------------------------------------------------------------------------------------------
// Private API don't use
//----------------------------------------------------------------------------------------------------------------------

/**
 * Enter the scheduler and potentially select a new thread to run.
 *
 * Must be called with interrupts disabled and returns with interrupts disabled.
 */
void scheduler_schedule(void);

/**
 * This will perform a schedule that wll only
 * come back after the deadline is reached or
 * someone actively woke up the thread
 */
void scheduler_schedule_deadline(uint64_t deadline);

/**
 * Unparks the requested thread, if it is currently parked.
 */
bool scheduler_try_unpark(thread_t* thread);

/**
 * Enqueues the requested thread for execution on the current core.
 * The thread is expected to be READY.
 */
void scheduler_enqueue(thread_t* thread);

//----------------------------------------------------------------------------------------------------------------------
// Public API
//----------------------------------------------------------------------------------------------------------------------

/**
 * Yield to another process
 */
void sched_yield(void);

/**
 * Get the currently running thread
 */
thread_t* get_current_thread(void);

/**
 * Disable preemption, supports nesting
 */
void preempt_disable(void);

/**
 * Enable preemption, needs to be called for every preempt_disable.
 * This will possibly preempt if required
 */
void preempt_enable(void);
