#pragma once

#include <stdnoreturn.h>
#include <stdint.h>

#include "lib/except.h"
#include "proc/thread.h"

/**
 * The amount of cores we have
 */
extern uint32_t g_cpu_count;

/**
 * Get the current CPU
 */
static inline uint32_t get_cpu_id(void) {
    return __builtin_ia32_rdpid();
}

void init_sched(void);

noreturn void sched_start_per_core(void);

void sched_queue(thread_t* thread);

thread_t* get_current_thread(void);

void preempt_disable(void);

void preempt_enable(void);
