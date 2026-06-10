#include "proc.h"

#include <stdatomic.h>
#include <stdint.h>

#include "alloc/alloc.h"

#include "lib/assert.h"
#include "lib/atomic.h"
#include "lib/except.h"
#include "lib/list.h"
#include "lib/log.h"
#include "lib/syscall.h"

#include "sync/mutex.h"
#include "uapi/page.h"
#include "wasm/jit.h"
#include "wasm/wasm.h"

#include "wasm_err.h"
#include "wasi.h"

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

wasm_proc_t* wasm_get_proc(wasm_proc_t* proc) {
    atomic_fetch_add_explicit(&proc->ref_count, 1, memory_order_acquire);
    return proc;
}

void wasm_put_proc(wasm_proc_t* proc) {
    size_t ref_count = atomic_fetch_sub_explicit(&proc->ref_count, 1, memory_order_release);
    ASSERT(ref_count >= 1);

    if (ref_count == 1) {
        atomic_fence_acquire();

        // free the wasm jit and module
        // TODO: sharing?
        wasm_module_jit_free(&proc->jit);
        wasm_module_free(&proc->module);

        // free the memory itself
        if (proc->memory_base != nullptr) {
            sys_mem_free(proc->memory_base);
        }

        // free the proc itself
        mem_free(proc);
    }
}

wasm_proc_t* wasm_current_proc(void* state_base) {
    wasm_state_t* state = containerof(state_base, wasm_state_t, state);
    return state->proc;
}

void wasm_thread_start(wasm_thread_start_args_t* _args) {
    // get a local copy so we won't need this anymore
    wasm_thread_start_args_t args = *_args;
    mem_free(_args);

    if (args.tid == 1) {
        // if this is the first thread of the process
        if (args.proc->jit.start_func != nullptr) {
            args.proc->jit.start_func(args.proc->memory_base, args.state_base);
        }

        // call the entry point (_start)
        if (args.proc->start != nullptr) {
            args.proc->start(args.proc->memory_base, args.state_base);
        }

    } else {
        // this is a secondary thread, call it
        ASSERT(args.proc->wasi_thread_start != nullptr);
        args.proc->wasi_thread_start(args.proc->memory_base, args.state_base, args.tid, args.arg);
    }

    // free the state
    mem_free(args.state_base);
    
    // release the process, we no longer need it
    wasm_put_proc(args.proc);
}

static err_t wasm_create_thread(wasm_proc_t* proc, uint32_t arg, uint32_t* out_tid) {
    err_t err = NO_ERROR;
    wasm_state_t* state = nullptr;

    // allocate the args for the thread
    wasm_thread_start_args_t* args = mem_alloc(sizeof(*args));
    CHECK_ERROR(args != nullptr, ERROR_OUT_OF_MEMORY);
    memset(args, 0, sizeof(*args));

    // allocate the wasm state
    size_t state_size = sizeof(wasm_state_t) + proc->jit.state_size;
    state = mem_alloc(state_size);
    CHECK_ERROR(state != nullptr, ERROR_OUT_OF_MEMORY);
    memset(state, 0, state_size);

    // save the proc
    state->proc = proc;

    // if there is an init state, initialize it
    if (proc->jit.state_size != 0) {
        memcpy(state->state, proc->jit.state_init, proc->jit.state_size);
    }

    // the process we are under
    args->proc = wasm_get_proc(proc);

    // generate the new tid, the first thread gets id of 1
    uint32_t tid = atomic_fetch_add_explicit(&proc->thread_id_gen, 1, memory_order_relaxed) + 1;
    if (tid > 1) {
        // this is a helper thread, ensure we have an entry point
        CHECK(proc->wasi_thread_start != nullptr);
    }
    args->tid = tid;

    // the argument
    args->arg = arg;

    // and the state base
    args->state_base = state->state;
    state = nullptr;

    // and actually create/start the thread
    CHECK_ERROR(sys_thread_create(args, "wasm"), ERROR_OUT_OF_MEMORY);
    args = nullptr;

    // output the tid
    if (out_tid != nullptr) {
        *out_tid = args->tid;
    }

cleanup:
    mem_free(state);
    mem_free(args);

    return err;
}

static void* wasm_resolve_import(void* arg, const char* module, const char* name, wasm_type_t* type) {
    if (strcmp(module, "wasi_snapshot_preview1") == 0) {
        return wasip1_resolve_import(name);
    } else {
        return nullptr;
    }
}

err_t wasm_create_proc(void* module, size_t module_size) {
    err_t err = NO_ERROR;

    // allocate the new proc
    wasm_proc_t* proc = mem_alloc(sizeof(*proc));
    CHECK_ERROR(proc != nullptr, ERROR_OUT_OF_MEMORY);
    memset(proc, 0, sizeof(*proc));

    // start with ref count of 1
    proc->ref_count = 1;

    // load the module
    RETHROW_WASM(wasm_load_module(&proc->module, module, module_size));

    // allocate the memory, and properly initialize it once
    proc->memory_base = sys_mem_reserve(SIZE_TO_PAGES(proc->module.memory.max), "wasm-memory");
    CHECK_ERROR(proc->memory_base != nullptr, ERROR_OUT_OF_MEMORY);

    // perform the initial bump
    proc->memory_size = proc->module.memory.min;
    void* base = sys_mem_bump(proc->memory_base, SIZE_TO_PAGES(proc->module.memory.min));
    CHECK_ERROR(base != nullptr, ERROR_OUT_OF_MEMORY);

    // and initialize the memory
    wasm_module_init_memory(&proc->module, proc->memory_base);

    // jit it 
    wasm_jit_config_t config = {
        // the import resolver
        .resolve_import = wasm_resolve_import,

        // we don't need the debug info
        .emit_debug_info = false,
        
        // please speed
        .optimize = true,
    };
    RETHROW_WASM(wasm_module_jit(&proc->module, &proc->jit, &config));

    // find the wasm entry point, only if sharing is enabled
    if (proc->module.memory.shared) {
        int64_t index = wasm_find_export(&proc->module, "wasi_thread_start");
        if (index >= 0) {
            // get the export
            wasm_export_t* export = &proc->module.exports[index];
            CHECK(export->kind == WASM_EXPORT_FUNC);

            // the func index so we can check it
            wasm_type_t* type = wasm_get_func(&proc->module, export->index);
            CHECK(type != nullptr);
            
            // ensure the signature is correct
            CHECK(type->arg_types_count == 2);
            CHECK(type->arg_types[0] == WASM_VALUE_TYPE_I32);
            CHECK(type->arg_types[1] == WASM_VALUE_TYPE_I32);
            CHECK(type->result_types_count == 0);

            // save it from the jit
            proc->wasi_thread_start = proc->jit.exports[index].func.address;
        }
    }

    // find the entry point (_start)
    int64_t index = wasm_find_export(&proc->module, "_start");
    if (index >= 0) {
        // get the export
        wasm_export_t* export = &proc->module.exports[index];
        CHECK(export->kind == WASM_EXPORT_FUNC);

        // the func index so we can check it
        wasm_type_t* type = wasm_get_func(&proc->module, export->index);
        CHECK(type != nullptr);
        
        // ensure the signature is correct
        CHECK(type->arg_types_count == 0);
        CHECK(type->result_types_count == 0);

        // save it from the jit
        proc->start = proc->jit.exports[index].func.address;
    }

    // and finally create the new thread
    RETHROW(wasm_create_thread(proc, 0, nullptr));

cleanup:
    if (proc != nullptr) {
        wasm_put_proc(proc);
    }

    return err;
}

int32_t wasm_host_memory_size(void* memory_base, void* state_base) {
    wasm_proc_t* proc = wasm_current_proc(state_base);
    return atomic_load_acquire(&proc->memory_size);
}

int32_t wasm_host_memory_grow(void* memory_base, void* state_base, int32_t new_page_count) {
    wasm_proc_t* proc = wasm_current_proc(state_base);

    mutex_lock(&proc->memory_lock);
    
    // the old and new sizes
    size_t current_size = atomic_load_relaxed(&proc->memory_size);
    size_t size_to_add = new_page_count * WASM_PAGE_SIZE;

    // bump the region
    void* result = sys_mem_bump(memory_base, SIZE_TO_PAGES(size_to_add));
    if (result == nullptr) {
        // either out of memory or we went over the page limit
        mutex_unlock(&proc->memory_lock);
        return -1;
    }

    // add the new size
    atomic_fetch_add_explicit(&proc->memory_size, size_to_add, memory_order_release);

    mutex_unlock(&proc->memory_lock);

    // return the page count
    return current_size / WASM_PAGE_SIZE;
}