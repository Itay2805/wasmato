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

__attribute__((force_align_arg_pointer))
int _start() {
    spidir_log_init(spidir_log_callback);
    spidir_log_set_max_level(SPIDIR_LOG_LEVEL_INFO);

    spidir_module_handle_t module = spidir_module_create();

    while (1);
}
