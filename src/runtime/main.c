#include "alloc/alloc.h"
#include "lib/assert.h"
#include "lib/defs.h"
#include "lib/except.h"
#include "lib/log.h"
#include "lib/syscall.h"

#include <spidir/log.h>

#include "proc/handle.h"
#include "proc/proc.h"
#include "proc/thread.h"
#include "wasi/file.h"
#include "wasi/wasip1.h"
#include "wasmato/wasmato.h"

uint64_t g_tsc_freq_hz = 0;

static void spidir_log_callback(spidir_log_level_t level, const char* module, size_t module_len, const char* message, size_t message_len) {
    switch (level) {
        default:
        case SPIDIR_LOG_LEVEL_TRACE: DEBUG("%.*s: %.*s", (int)module_len, module, (int)message_len, message); break;
        case SPIDIR_LOG_LEVEL_DEBUG: DEBUG("%.*s: %.*s", (int)module_len, module, (int)message_len, message); break;
        case SPIDIR_LOG_LEVEL_INFO: TRACE("%.*s: %.*s", (int)module_len, module, (int)message_len, message); break;
        case SPIDIR_LOG_LEVEL_WARN: WARN("%.*s: %.*s", (int)module_len, module, (int)message_len, message); break;
        case SPIDIR_LOG_LEVEL_ERROR: ERROR("%.*s: %.*s", (int)module_len, module, (int)message_len, message); break;
    }
}

static void main(void) {
    err_t err = WASI_ERRNO_SUCCESS;

    // initialize the tsc freq so we can sync with it nicely
    g_tsc_freq_hz = sys_early_get_tsc_freq();

    // ensure we only enter the main function once
    static bool init_once = false;
    CHECK(!init_once);
    init_once = true;

    TRACE("From main thread!");

    // get the initrd from the kernel before we are done
    size_t initrd_size = sys_early_get_initrd_size();
    void* initrd = mem_alloc(initrd_size);
    CHECK(initrd != nullptr);
    sys_early_get_initrd(initrd);

    // save the RSDP for wasm to access
    g_acpi_rsdp = sys_early_get_rsdp();

    // we can now mark that the early done is over,
    // and we can free the main stacks
    sys_early_done();

    // setup the spidir logging
    spidir_log_init(spidir_log_callback);
    spidir_log_set_max_level(SPIDIR_LOG_LEVEL_WARN);

    // TODO: set this correctly
    wasi_file_t* debug_output = wasi_file_create();
    debug_output->stats.fs_filetype = WASI_FILETYPE_CHARACTER_DEVICE;
    debug_output->stats.fs_rights_base = WASI_RIGHTS_FD_WRITE;

    // create the initrd process, give it the debug output 
    // as both stdout/stderr
    // TODO: give it a dummy stdin
    proc_handle_t handles[] = {
        (proc_handle_t){ 
            .fd = 1,
            .object = &debug_output->object,
            .rights = RIGHT_WRITE
        },
        (proc_handle_t){ 
            .fd = 2,
            .object = &debug_output->object,
            .rights = RIGHT_WRITE
        },
    };
    RETHROW(wasm_create_proc(
        WASM_PROC_TYPE_ACPID, 
        initrd, initrd_size, 
        handles, ARRAY_LENGTH(handles)
    ));

cleanup:
    ASSERT(!IS_ERROR(err), "Failed to start the system");
    (void)err;
}

/**
 * The design makes it so all thread entries start from here,
 * we make sure that only
 */
__attribute__((force_align_arg_pointer, nocf_check))
void _start(void* arg) {
    if (arg == nullptr) {
        main();
    } else {
        ASSERT(arg != nullptr);
        wasm_thread_start(arg);
    }
    sys_thread_exit();
}
