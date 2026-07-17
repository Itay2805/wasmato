#include "thread.h"
#include "alloc/alloc.h"
#include "lib/list.h"
#include "lib/stb_sprintf.h"
#include "lib/syscall.h"


noreturn void wasm_thread_exit(void* state_base) {
    // get the actual state
    wasm_state_t* state = containerof(state_base, wasm_state_t, state);
    state_base = nullptr;

    // we will need the proc
    wasm_proc_t* proc = state->proc;

    // free the state, since we no longer need it 
    mem_free(state);
    state = nullptr;

    // release the proc itself, we don't need it
    wasm_put_proc(proc);
    proc = nullptr;

    // finally hard-exit, to ensure that 
    // everything ends well
    sys_thread_exit();
}

void wasm_thread_start(wasm_thread_start_args_t* args) {
    // get a local copy so we won't need this anymore
    wasm_state_t* state = args->state;
    int32_t tid = args->tid;
    int32_t arg = args->arg;
    mem_free(args);
    args = nullptr;

    wasm_proc_t* proc = state->proc;

    if (tid == 1) {
        // if this is the first thread of the process
        if (proc->jit.start_func != nullptr) {
            proc->jit.start_func(proc->memory_base, state->state);
        }

        // call the entry point (_start)
        if (proc->start != nullptr) {
            proc->start(proc->memory_base, state->state);
        }

    } else {
        // this is a secondary thread, call it
        ASSERT(proc->wasi_thread_start != nullptr);
        proc->wasi_thread_start(proc->memory_base, state->state, tid, arg);
    }

    // free the state
    wasm_thread_exit(state->state);
}

err_t wasm_create_thread(wasm_proc_t* proc, uint32_t arg, int32_t* out_tid) {
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

    // save the proc, we take a ref to it
    state->proc = wasm_get_proc(proc);

    // if there is an init state, initialize it
    if (proc->jit.state_size != 0) {
        memcpy(state->state, proc->jit.state_init, proc->jit.state_size);
    }

    // generate the new tid, the first thread gets id of 1
    // TODO: handle tid overflow
    int32_t tid = (int32_t)(atomic_fetch_add_explicit(&proc->thread_id_gen, 1, memory_order_relaxed) + 1);
    CHECK_ERROR(tid > 0, ERROR_OUT_OF_MEMORY);

    if (tid > 1) {
        // this is a helper thread, ensure we have an entry point
        CHECK(proc->wasi_thread_start != nullptr);
    }
    args->tid = tid;

    // the argument
    args->arg = arg;

    // and the state base
    args->state = state;
    state = nullptr;

    // setup a name
    char name[128] = "";
    const char* module_name = "wasm";
    if (proc->module.module_name != nullptr) {
        module_name = proc->module.module_name;
    }
    stbsp_snprintf(name, sizeof(name), "%s#%d#%d", module_name, proc->process_id, tid);

    // and actually create/start the thread
    CHECK_ERROR(sys_thread_create(args, name), ERROR_OUT_OF_MEMORY);
    args = nullptr;

    // output the tid
    if (out_tid != nullptr) {
        *out_tid = tid;
    }

cleanup:
    mem_free(state);
    mem_free(args);

    return err;
}
