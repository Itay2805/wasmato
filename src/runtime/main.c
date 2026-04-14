#include "alloc/alloc.h"
#include "lib/assert.h"
#include "lib/except.h"
#include "lib/log.h"
#include "lib/stb_ds.h"
#include "lib/syscall.h"
#include "wasm/module.h"
#include "wasm/jit/jit.h"

static void main(void) {
    err_t err = NO_ERROR;

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

    // setup the jit
    wasm_jit_init();

    // load the module
    wasm_module_t module = {};
    wasm_jit_t jit = {};
    RETHROW(wasm_load_module(&module, initrd, initrd_size));
    RETHROW(wasm_jit_module(&module, &jit));

    TRACE("Exports:");
    for (int i = 0; i < shlen(module.exports); i++) {
        TRACE("\t%s(func%d) - %p", module.exports[i].key, module.exports[i].index, wasm_jit_get_function(&jit, module.exports[i].key));
    }

cleanup:
    wasm_jit_free(&jit);
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
