#pragma once

#include "arch/intrin.h"

static inline bool is_irq_enabled() { return __builtin_ia32_readeflags_u64() & BIT9; }

static inline bool irq_save() {
    bool status = is_irq_enabled();
    irq_disable();
    return status;
}

static inline void irq_restore(bool irq_status) {
    if (irq_status) {
        irq_enable();
    }
}
