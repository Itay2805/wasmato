#include "alloc/alloc.h"
#include "lib/assert.h"
#include "lib/log.h"
#include "uapi/syscall.h"

void main(void* arg) {
    TRACE("From main thread!");

    // get the initrd from the kernel before we are done
    void* initrd = mem_alloc(sys_early_get_initrd_size());
    ASSERT(initrd != nullptr);
    sys_early_get_initrd(initrd);

    // we can now mark that the early done is over,
    // and we can free the main stacks
    sys_early_done();
}
