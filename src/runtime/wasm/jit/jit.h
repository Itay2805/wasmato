#pragma once

#include "wasm/module.h"
#include "lib/except.h"

typedef struct wasm_jit {
    // the jit area of the code, this is both the
    // read-exec and the read-only parts
    void* code;
    size_t code_page_count;

    // the addresses of exported functions
    struct {
        char* key;
        void* value;
    }* exported_functions;

    // the size in bytes needed for all the globals
    size_t globals_size;
} wasm_jit_t;

void wasm_jit_free(wasm_jit_t* jit);

void wasm_jit_init(void);

/**
 * Prepare and create a wasm thread from teh given wasm module
 */
err_t wasm_jit_module(wasm_module_t* module, wasm_jit_t* jit);

/**
 * Get an address that can be used to call an exported function
 */
void* wasm_jit_get_function(wasm_jit_t* module, const char* name);
