#pragma once

#include <stdint.h>

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
