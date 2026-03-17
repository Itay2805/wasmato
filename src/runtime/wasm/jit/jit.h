#pragma once

#include "wasm/module.h"
#include "lib/except.h"
#include "proc/thread.h"

void wasm_jit_init(void);

/**
 * Prepare and create a wasm thread from teh given wasm module
 */
err_t wasm_jit_module(wasm_module_t* module);
