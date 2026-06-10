#include "wasi.h"
#include "wasm.h"
#include "alloc/alloc.h"
#include "lib/assert.h"
#include "lib/except.h"
#include "lib/log.h"
#include "lib/stb_ds.h"
#include "lib/syscall.h"

#include "wasm_err.h"
#include "wasm/wasm.h"
#include "wasm/jit.h"

uint64_t g_tsc_freq_hz = 0;

static void* wasm_resolve_import(void* arg, const char* module, const char* name, wasm_type_t* type) {
    if (strcmp(module, "wasi_snapshot_preview1") == 0) {
        return wasip1_resolve_import(name);
    } else {
        return nullptr;
    }
}

static void main(void) {
    err_t err = NO_ERROR;
    wasm_entry_args_t* args = nullptr;

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

    // we can now mark that the early done is over,
    // and we can free the main stacks
    sys_early_done();

    args = mem_alloc(sizeof(*args));
    CHECK(args != nullptr);
    memset(args, 0, sizeof(*args));

    // actually load it
    RETHROW_WASM(wasm_load_module(&args->module, initrd, initrd_size));

    // jit it
    wasm_jit_config_t config = {
        .optimize = true,
        .resolve_import = wasm_resolve_import
    };
    RETHROW_WASM(wasm_module_jit(&args->module, &args->jit, &config));

    // start a new thread with it
    CHECK(sys_thread_create(args, "wasm"));
    args = nullptr;

cleanup:
    if (args != nullptr) {
        wasm_module_jit_free(&args->jit);
        wasm_module_free(&args->module);
        mem_free(args);
    }

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
        wasm_entry_point(arg);
    }
    sys_thread_exit();
}
