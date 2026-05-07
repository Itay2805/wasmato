#include "wasi.h"

#include "lib/defs.h"
#include "lib/log.h"
#include "lib/string.h"
#include "lib/syscall.h"

typedef struct wasip1_export {
    const char* name;
    void* func;
} wasip1_export_t;

static void wasip1_proc_exit(void* memory_base, void* state_base, uint32_t rval) {
    TRACE("proc_exit called! - 0x%x", rval);
    sys_thread_exit();
}

static const wasip1_export_t m_wasip1_exports[] = {
    { "proc_exit", wasip1_proc_exit }
};

void* wasip1_resolve_import(const char* name) {
    for (int i = 0; i < ARRAY_LENGTH(m_wasip1_exports); i++) {
        if (strcmp(m_wasip1_exports[i].name, name) == 0) {
            return m_wasip1_exports[i].func;
        }
    }
    return nullptr;
}
