#pragma once

#include <stdatomic.h>
#include <stdint.h>
#include <wasm/wasm.h>
#include <wasm/jit.h>

#include <lib/except.h>
#include "sync/mutex.h"


typedef struct wasm_proc {
    /**
     * This is basically the thread count
     */
    atomic_size_t ref_count;

    /**
     * The module that owns this process
     */
    wasm_module_t module;

    /**
     * The jitted instance of the module
     */
    wasm_module_jit_t jit;

    /**
     * The thread's entry point
     */
    void (*wasi_thread_start)(void* memory_base, void* state_base, uint32_t tid, uint32_t start_arg);

    /**
     * The actual entry point
     */
    void (*start)(void* memory_base, void* state_base);

    /**
     * The memory base shared for the process
     */
    void* memory_base;

    /**
     * The current memory size
     */
    atomic_size_t memory_size;

    /**
     * The lock to synchronize 
     * different memory bumping
     */
    mutex_t memory_lock;

    /**
     * For generating thread ids
     */
    _Atomic(uint32_t) thread_id_gen;

    /**
     * The id of the process
     */
    int32_t process_id;

    // TODO: thread list?
} wasm_proc_t;

wasm_proc_t* wasm_get_proc(wasm_proc_t* proc);
void wasm_put_proc(wasm_proc_t* proc);

wasm_proc_t* wasm_current_proc(void* state_base);

typedef struct wasm_state {
    /**
     * The actual process
     */
    wasm_proc_t* proc;

    /**
     * The actual state
     */
    char state[0];
} wasm_state_t;

typedef struct wasm_thread_start_args {
    /**
     * The TID of the thread, 1 is the first thread, 
     * which does not use the special entry point
     */
    uint32_t tid;

    /**
     * The argument to pass to the thread
     */
    uint32_t arg;

    /**
     * The thread's own state base
     */
    wasm_state_t* state;
} wasm_thread_start_args_t;

void wasm_thread_start(wasm_thread_start_args_t* args);

void wasm_thread_exit(void* state_base);

err_t wasm_create_proc(void* module, size_t module_size);
