#include "lib/log.h"

#include <stdarg.h>

#include "arch/intrin.h"
#include "sync/spinlock.h"
#include "lib/defs.h"
#include "lib/printf.h"

static irq_spinlock_t m_debug_lock = IRQ_SPINLOCK_INIT;

static bool m_e9_enabled = false;

void init_early_logging() {
    // detect e9 support
    m_e9_enabled = __inbyte(0xE9) == 0xE9;
}

void putchar_(char c) {
    __outbyte(0xE9, c);
}

void debug_print(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    bool irq_state = irq_spinlock_acquire(&m_debug_lock);
    vprintf_(fmt, args);
    irq_spinlock_release(&m_debug_lock, irq_state);
    va_end(args);
}
