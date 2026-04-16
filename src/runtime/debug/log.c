#include "lib/log.h"

#include <stdarg.h>

#include "lib/stb_sprintf.h"
#include "lib/syscall.h"

static char* debug_print_cb(const char* buf, void* user, int len) {
    sys_debug_print(buf, len);
    return user;
}

void debug_print(const char* fmt, ...) {
    char buffer[STB_SPRINTF_MIN];
    va_list ap = {};
    va_start(ap, fmt);
    stbsp_vsprintfcb(debug_print_cb, buffer, buffer, fmt, ap);
    va_end(ap);
}

void rust_platform_panic(const char* message, size_t message_len) {
    ERROR("%.*s", (int)message_len, message);
    __builtin_trap();
}
