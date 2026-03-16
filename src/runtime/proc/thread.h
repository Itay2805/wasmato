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

/**
 * The various states in which a thread can be:
 *
 * - IDLE: newly-created, not running
 * - DEAD: no longer running, will be freed when when all refs are released
 * - READY: currently scheduled out, but not parked (blocked) on anything
 * - RUNNING: currently running on a CPU
 * - PARKING: preparing to park until woken, but currently still running
 * - PARKED: waiting until an explicit unpark is issued
 *
 * The following transitions are allowed:
 *
 *         IDLE
 *          |
 *          V
 *    +-> READY
 *    |     |
 *    ^     V
 *    +- RUNNING -> DEAD
 *    |     |
 *    ^     V
 *    +- PARKING
 *    |     |
 *    ^     V
 *    +- PARKED
 *
 * - IDLE -> READY: performed non-atomically by `thread_start` (should happen at most once).
 *
 * - READY -> RUNNING: performed non-atomically only by the run queue in which the thread is
 *   currently waiting to run.
 *
 * - RUNNING -> DEAD: performed non-atomically by the scheduler when the thead calls `thread_exit`
 *   (can happen at most once).
 *
 * - RUNNING -> PARKING: performed non-atomically only by the thread itself when preparing to park;
 *   exists to avoid races in condition-based parking (see `atomic_wait`) when checking the wait
 *   condition itself.
 *
 * - PARKING -> PARKED: performed as an atomic CAS by the scheduler once a PARKING thread has been
 *   switched out; may race against PARKING -> READY transitions performed by concurrent
 *   `scheduler_try_unpark` calls.
 *
 * - PARKING -> READY: performed as an atomic CAS by `scheduler_try_unpark` to abort an in-progress
 *   park operation; may race against the scheduler's PARKING -> PARKED transition and concurrent
 *   `scheduler_try_unpark` calls.
 *
 * - PARKED -> READY: performed as an atomic CAS by `scheduler_try_unpark` to unpark a thread; may
 *   race against concurrent `scheduler_try_unpark` calls.
 *
 */
typedef enum thread_state {
    THREAD_STATE_IDLE,
    THREAD_STATE_DEAD,
    THREAD_STATE_READY,
    THREAD_STATE_RUNNING,
    THREAD_STATE_PARKING,
    THREAD_STATE_PARKED,
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
 *
 * This function should be called at most once for every created thread.
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
