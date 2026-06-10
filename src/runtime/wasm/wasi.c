#include "wasi.h"

#include "lib/defs.h"
#include "lib/log.h"
#include "lib/string.h"
#include "lib/syscall.h"
#include "wasm/proc.h"

typedef struct wasip1_export {
    const char* name;
    void* func;
} wasip1_export_t;

static void wasip1_proc_exit(void* memory_base, void* state_base, uint32_t rval) {
    wasm_proc_t* proc = wasm_current_proc(state_base);

    // we no longer need both of these
    memory_base = nullptr;
    state_base = nullptr;

    // TODO: signal that all threads should be killed (somehow)

    // release the process, the last thread to exit 
    // will actually free it
    wasm_put_proc(proc);

    // TODO: remove this
    TRACE("proc_exit: process exited with 0x%x", rval);

    // and finally just exit
    sys_thread_exit();
}

static void wasip1_sched_yield(void* memory_base, void* state_base) {
    sys_thread_yield();
}

static const wasip1_export_t m_wasip1_exports[] = {
    { "proc_exit", wasip1_proc_exit },
    { "sched_yield", wasip1_sched_yield }
};

void* wasip1_resolve_import(const char* name) {
    for (int i = 0; i < ARRAY_LENGTH(m_wasip1_exports); i++) {
        if (strcmp(m_wasip1_exports[i].name, name) == 0) {
            return m_wasip1_exports[i].func;
        }
    }
    return nullptr;
}
