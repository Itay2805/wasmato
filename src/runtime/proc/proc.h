#pragma once

#include <stdatomic.h>
#include <stdint.h>
#include <wasm/wasm.h>
#include <wasm/jit.h>
#include <lib/except.h>

#include "sync/mutex.h"
#include "handle.h"

typedef enum wasm_proc_type : uint8_t {
    /**
     * Any normal user memory
     */
    WASM_PROC_TYPE_USER,

    /**
     * The ACPID process is allowed to get the rsdp 
     * and to map any physical memory it wants
     */
    WASM_PROC_FLAG_ACPID,
} wasm_proc_type_t;

typedef struct wasm_proc {
    /**
     * Process flags
     */
    wasm_proc_type_t type;

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

    /**
     * The handle table
     */
    handle_table_t handles;
} wasm_proc_t;

wasm_proc_t* wasm_get_proc(wasm_proc_t* proc);
void wasm_put_proc(wasm_proc_t* proc);

wasm_proc_t* wasm_current_proc(void* state_base);

err_t wasm_create_proc(wasm_proc_type_t type, void* module, size_t module_size);
