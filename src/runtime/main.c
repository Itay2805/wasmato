#include "alloc/alloc.h"
#include "lib/assert.h"
#include "lib/except.h"
#include "lib/log.h"
#include "uapi/syscall.h"
#include "wasm/module.h"
#include "wasm/jit/jit.h"

void main(void* arg) {
    err_t err = NO_ERROR;

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
    RETHROW(wasm_load_module(&module, initrd, initrd_size));
    RETHROW(wasm_jit_module(&module));

cleanup:
    wasm_module_free(&module);

    (void)err;
}
