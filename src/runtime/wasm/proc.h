#pragma once

#include <stdatomic.h>
#include <stdint.h>
#include <wasm/wasm.h>
#include <wasm/jit.h>

#include <lib/except.h>
#include "lib/defs.h"
#include "sync/mutex.h"
#include "wasm/file.h"

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
     * The FD table + its lock
     */
    file_t** fd_table;
    mutex_t fd_table_lock;
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

extern uint64_t g_acpi_rsdp;

void wasm_thread_start(wasm_thread_start_args_t* args);

void wasm_thread_exit(void* state_base);

err_t wasm_create_proc(wasm_proc_type_t type, void* module, size_t module_size);

/**
 * Get a file from an fd, increases the ref count
 */
file_t* wasm_proc_get_fd(wasm_proc_t* proc, int fd);

/**
 * Close an existing fd from the file table
 */
bool wasm_proc_close_fd(wasm_proc_t* proc, int fd);

/**
 * Register a file to the process's fd table, takes the 
 * reference from the caller
 */
int wasm_proc_register_file(wasm_proc_t* proc, file_t* file);

/**
 * Register a file to the process's fd table, takes the 
 * reference from the caller
 */
int wasm_proc_register_file_at(wasm_proc_t* proc, file_t* file, int fd);
