#pragma once
#include "wasm/jit.h"
#include "wasm/wasm.h"


typedef struct wasm_entry_args {
    /**
     * The module that we just jitted
     */
    wasm_module_t module;

    /**
     * The jit result
     */
    wasm_module_jit_t jit;
} wasm_entry_args_t;

void wasm_entry_point(wasm_entry_args_t* args);
