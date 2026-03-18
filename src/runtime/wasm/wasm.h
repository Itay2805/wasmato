#pragma once

#include <stddef.h>

#include "module.h"
#include "jit/jit.h"

typedef struct wasm_instance {
    // the module of this instance
    wasm_module_t* module;

    // the jit output, already linked and ready to run
    wasm_jit_t* output;

    /**
     * The memory VMAR
     */
    void* memory;

    /**
     * The globals of the instance
     */
    void* globals;
} wasm_instance_t;
