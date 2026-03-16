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
     * The state of the thread when it is just created
     */
    THREAD_STATE_IDLE,

    /**
     * The thread is dead, it will be freed once all
     * refs run out
     */
    THREAD_STATE_DEAD,

    /**
     * The thread is currently being parked and will
     * transition to THREAD_STATE_PARKED once scheduled out
     */
    THREAD_STATE_PARKING,

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
    list_entry_t link;

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
     * The thread state
     */
    _Atomic(thread_state_t) state;

    //
    // FPU context
    //
    alignas(64) uint8_t extended_state[];
} thread_t;

void init_threads(size_t tls_size);

thread_t* thread_vcreate(thread_entry_t entry_point, void* arg, const char* name_fmt, va_list args);

/**
 * Create a new thread
 */
thread_t* thread_create(thread_entry_t entry_point, void* arg, const char* name_fmt, ...);

/**
 * Start the thread.
 *
 * This adopts a single reference on the thread;
 * it will be automatically `thread_put` on exit.
 */
void thread_start(thread_t* thread);

/**
 * Increase the ref count of the thread
 */
thread_t* thread_get(thread_t* thread);

/**
 * Decrease the ref count of the thread
 */
void thread_put(thread_t* thread);

/**
 * Exit from the thread, losing its own reference
 */
void thread_exit(void);

/**
 * Put the thread to sleep
 */
void thread_sleep(size_t ms);
