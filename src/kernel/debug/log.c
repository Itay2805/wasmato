#include "lib/log.h"

#include <stdarg.h>

#include "arch/intrin.h"
#include "sync/spinlock.h"
#include "lib/defs.h"
#include "lib/printf.h"

static spinlock_t m_debug_lock = SPINLOCK_INIT;

static bool m_e9_enabled = false;

INIT_CODE void init_early_logging() {
    // detect e9 support
    m_e9_enabled = __inbyte(0xE9) == 0xE9;
}

void debug_print_raw(const char* buf, size_t size) {
    for (int i = 0; i < size; i++) {
        __outbyte(0xE9, buf[i]);
    }
}

int debug_print_cb(void* user, const char* buf, size_t size) {
    debug_print_raw(buf, size);
    return 0;
}

void debug_print(const char* fmt, ...) {
    va_list args = {};
    va_start(args, fmt);

    bool state = irq_save();
    spinlock_acquire(&m_debug_lock);
    vcprintf(debug_print_cb, nullptr, SIZE_MAX, fmt, args);
    spinlock_release(&m_debug_lock);
    irq_restore(state);

    va_end(args);
}
