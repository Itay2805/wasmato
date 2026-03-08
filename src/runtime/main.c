#include "lib/tsc.h"
#include "lib/log.h"
#include "lib/printf.h"
#include "uapi/syscall.h"

#include "spidir/log.h"
#include "spidir/module.h"

static void spidir_log_callback(spidir_log_level_t level, const char* module, size_t module_len, const char* message, size_t message_len) {
    switch (level) {
        case SPIDIR_LOG_LEVEL_ERROR: ERROR("%.*s: %.*s", (int)module_len, module, (int)message_len, message);
        case SPIDIR_LOG_LEVEL_WARN: WARN("%.*s: %.*s", (int)module_len, module, (int)message_len, message);
        case SPIDIR_LOG_LEVEL_INFO: TRACE("%.*s: %.*s", (int)module_len, module, (int)message_len, message);
        case SPIDIR_LOG_LEVEL_DEBUG: DEBUG("%.*s: %.*s", (int)module_len, module, (int)message_len, message);
        case SPIDIR_LOG_LEVEL_TRACE: TRACE("%.*s: %.*s", (int)module_len, module, (int)message_len, message);
        default: TRACE("%.*s: %.*s", (int)module_len, module, (int)message_len, message);
    }
}

__attribute__((interrupt))
static void timer_handler(interrupt_frame_t* frame) {
    syscall2(SYSCALL_DEBUG_PRINT, "TIMER!\n", sizeof("TIMER!\n") - 1);
}

uint64_t g_tsc_freq_hz;

__attribute__((force_align_arg_pointer))
int _start(int cpu) {
    TRACE("\tUsermode entered on CPU #%d", cpu);
    if (cpu != 0) {
        while (1);
    }

    g_tsc_freq_hz = sys_early_timer_get_freq();
    uint8_t vector = sys_early_timer_get_vector();
    sys_early_interrupt_set_handler(vector, timer_handler);

    sys_timer_set_deadline(tsc_ms_deadline(1000));
    asm("sti");
    while (1);
}
