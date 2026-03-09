#pragma once

typedef struct runtime_params {
    /**
     * The TSC frequency as calculated by the kernel
     */
    uint64_t tsc_freq;

    /**
     * The stack allocated for the cpu, so it can be reused or freed
     */
    void* stack;

    /**
     * The size of the TLS data without the TCB
     */
    size_t tls_size;

    /**
     * The id of the current cpu
     */
    uint32_t cpu_id;

    /**
     * The vector of the timer, anything between this vector
     * and 0xFF can be used by the user
     */
    uint8_t timer_vector;

    /**
     * The first and last vector available for the usermode to use,
     * not including the timer vector
     */
    uint8_t first_vector;
    uint8_t last_vector;
} runtime_params_t;
