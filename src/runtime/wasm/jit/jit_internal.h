#pragma once

#include "wasm/module.h"
#include "spidir/module.h"


typedef struct jit_function {
    spidir_function_t function;
    bool inited;
} jit_function_t;

typedef struct jit_context {
    spidir_module_handle_t spidir;
    wasm_module_t* module;

    // the jit functions
    jit_function_t* functions;

    // queue of functions to jit
    uint32_t* queue;
} jit_context_t;

