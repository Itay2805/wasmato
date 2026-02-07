#include "lib/log.h"
#include "lib/printf.h"
#include "uapi/syscall.h"

__attribute__((force_align_arg_pointer))
int _start() {
    TRACE("Hello from usermode!");
    while (1);
}
