#pragma once

#include <stdalign.h>
#include <stdatomic.h>

#include "lib/list.h"
#include "sync/spinlock.h"

typedef void (*thread_entry_t)(void* arg);

typedef struct tcb {
    struct tcb* tcb;
} tcb_t;

#define TCB ((__seg_fs tcb_t*)(0))

typedef enum thread_state {
    /**
     * The thread is dead, it will be freed once all
     * refs run out
     */
    THREAD_STATE_DEAD,

    /**
     * The thread is currently parked, and is not
     * on any run queue
     */
    THREAD_STATE_PARKED,

    /**
     * The thread is ready is on a run queue
     */
    THREAD_STATE_READY,

    /**
     * The thread is currently running
     */
    THREAD_STATE_RUNNING,
} thread_state_t;

typedef struct thread {
    /**
     * The stack of the thread
     * MUST NOT MOVE THE FIELD
     */
    void* rsp;

    /**
     * The shadow stack of the thread
     * MUST NOT MOVE THE FIELD
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

    //
    // Misc thread context
    //

    /**
     * The name of the thread, for debug
     */
    char* name;

    /**
     * Ref count on the thread
     */
    atomic_size_t ref_count;

    /**
     * Lock that protects the thread state
     */
    irq_spinlock_t lock;

    /**
     * The thread state
     */
    thread_state_t state;

    //
    // FPU context
    //
    alignas(64) uint8_t fpu[];
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

void thread_get(thread_t* thread);
void thread_put(thread_t* thread);
