#pragma once

#include "lib/list.h"

typedef void (*thread_entry_t)(void* arg);

typedef struct tcb {
    struct tcb* tcb;
} tcb_t;

#define TCB ((__seg_fs tcb_t*)(0))

typedef struct thread {
    /**
     * The stack of the thread
     */
    void* rsp;

    /**
     * The shadow stack of the thread
     */
    void* ssp;

    //
    // CPU Context
    //

    void* stack;

    /**
     * The thread control block (for thread locals)
     */
    tcb_t* tcb;

    //
    // Scheduler context
    //

    /**
     * The runqueue link when the thread is active
     */
    list_entry_t run_queue_link;

    /**
     * is the thread parked
     */
    bool parked;

    /**
     * The name of the thread, for debug
     */
    char* name;

    //
    // FPU context
    //
    // TODO: this
} thread_t;

void init_threads(size_t tls_size);

thread_t* thread_vcreate(thread_entry_t entry_point, void* arg, const char* name_fmt, va_list args);

static inline thread_t* thread_create(thread_entry_t entry_point, void* arg, const char* name_fmt, ...) {
    va_list args = {};
    va_start(args, name_fmt);
    thread_t* thread = thread_vcreate(entry_point, arg, name_fmt, args);
    va_end(args);
    return thread;
}
