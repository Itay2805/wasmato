#pragma once

#include <stdnoreturn.h>

#include "lib/except.h"

/**
 * Load and start the runtime
 */
INIT_CODE err_t runtime_load_and_start(void);

/**
 * This thunk is used to call into a usermode thread
 */
void runtime_thread_entry_thunk(void* arg);
