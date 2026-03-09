#include <stdatomic.h>

#include "arch/intrin.h"
#include "lib/tsc.h"
#include "lib/log.h"
#include "lib/printf.h"
#include "uapi/syscall.h"

#include "spidir/log.h"
#include "spidir/module.h"
#include "uapi/entry.h"

// This is used by the tsc header, initialize it in here
uint64_t g_tsc_freq_hz;

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

__attribute__((force_align_arg_pointer))
int _start(runtime_params_t* params) {
    static atomic_bool sched_ready = false;

    TRACE("runtime: Entered on CPU #%d", params->cpu_id);
    if (params->cpu_id != 0) {
        // for secondary cpus just wait until we are done the init
        // and the scheduler is ready so we can start scheduling
        while (!sched_ready) {
            cpu_relax();
        }

        TRACE("TODO: startup scheduler on core %d", params->cpu_id);

    } else {
        // setup the tsc freq so we can access it
        g_tsc_freq_hz = params->tsc_freq;

        // TODO: setup the scheduler

        // TODO: setup interrupt handling

        // we are done, let the kernel know we won't
        // need anything else
        sys_early_done();

        // let the other cores know that we are
        // ready to run
        sched_ready = true;

        // TODO: startup scheduler
        TRACE("TODO: startup scheduler on core %d", params->cpu_id);
    }

    ERROR("TODO: panic");
    while (1);
}
