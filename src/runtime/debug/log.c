#include "lib/log.h"

#include <stdarg.h>

#include "lib/printf.h"
#include "uapi/syscall.h"

void debug_print(const char* fmt, ...) {
    char buffer[512];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf_(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    syscall2(SYSCALL_DEBUG_PRINT, buffer, len);
}

void rust_platform_panic(const char* message, size_t message_len) {
    ERROR("%.*s", (int)message_len, message);
    __builtin_trap();
}
