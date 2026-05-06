#include "alloc/alloc.h"
#include "lib/assert.h"
#include "lib/except.h"
#include "lib/log.h"
#include "lib/stb_ds.h"
#include "lib/syscall.h"

#include "wasm_err.h"
#include "wasm/wasm.h"
#include "wasm/jit.h"

static void main(void) {
    err_t err = NO_ERROR;

    wasm_module_t module = {};
    wasm_module_jit_t jit = {};

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

    // actually load it
    RETHROW_WASM(wasm_load_module(&module, initrd, initrd_size));

    wasm_jit_config_t config = {
        .optimize = true,
    };
    RETHROW_WASM(wasm_module_jit(&module, &jit, &config));

cleanup:
    wasm_module_jit_free(&jit);
    wasm_module_free(&module);

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
        // TODO: wasm entry point
    }
    sys_thread_exit();
}
