#include "lib/log.h"
#include "uapi/syscall.h"

void main(void* arg) {
    TRACE("From main thread!");

    // we can now mark that the early done is over,
    // and we can free the main stacks
    sys_early_done();
}
