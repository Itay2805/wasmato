#include "wasi.h"

#include "lib/defs.h"
#include "lib/list.h"
#include "lib/log.h"
#include "lib/string.h"
#include "lib/syscall.h"
#include "wasm/errno.h"
#include "wasm/proc.h"

typedef enum wasi_clockid : uint32_t {
    WASI_CLOCKID_REALTIME = 0,
    WASI_CLOCKID_MONOTONIC = 1,
    WASI_CLOCKID_PROCESS_CPUTIME_ID = 2,
    WASI_CLOCKID_THREAD_CPUTIME_ID = 3,
} wasi_clockid_t;

typedef uint64_t wasi_timestamp_t;

static wasi_errno_t wasip1_clock_time_get(void* memory_base, void* state_base, wasi_clockid_t id, wasi_timestamp_t precision, uint32_t _retptr0) {
    wasi_timestamp_t* retptr0 = memory_base + _retptr0;

    switch (id) {
        default:
            WARN("wasi: unknown clock id %d", id);
            return WASI_ERRNO_NOTSUP;
    }

    return 0;
}

static void wasip1_proc_exit(void* memory_base, void* state_base, uint32_t rval) {
    wasm_proc_t* proc = wasm_current_proc(state_base);

    // TODO: remove this
    TRACE("proc_exit: process exited with 0x%x", rval);

    // and we can exit
    wasm_thread_exit(state_base);
}

static void wasip1_sched_yield(void* memory_base, void* state_base) {
    sys_thread_yield();
}

typedef struct wasip1_export {
    const char* name;
    void* func;
} wasip1_export_t;

static const wasip1_export_t m_wasip1_exports[] = {
    { "clock_time_get", wasip1_clock_time_get },
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
