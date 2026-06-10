#include "alloc/alloc.h"
#include "lib/assert.h"
#include "lib/except.h"
#include "lib/log.h"
#include "lib/syscall.h"

#include "wasm/proc.h"

uint64_t g_tsc_freq_hz = 0;

static void main(void) {
    err_t err = NO_ERROR;

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

    // create the initrd process
    RETHROW(wasm_create_proc(initrd, initrd_size));

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
