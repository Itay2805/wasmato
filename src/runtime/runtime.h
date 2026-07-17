#pragma once

#include "wasm/wasm.h"

typedef struct runtime_function {
    const char* const name;
    void* const address;
    const wasm_value_type_t ret_type;
    const wasm_value_type_t* const arg_types;
} runtime_function_t;

#define RUNTIME_FUNCTION_MAKE_SIG(x) WASM_VALUE_TYPE_##x
#define RUNTIME_FUNCTION_SIG(...) (const wasm_value_type_t[]){ MAP(RUNTIME_FUNCTION_MAKE_SIG, COMMA, ## __VA_ARGS__, INVALID) }

#define RUNTIME_FUNCTION(_prefix, _func, _ret_type, ...) \
    { \
        .name = #_func, \
        .address = _prefix##_##_func, \
        .ret_type = WASM_VALUE_TYPE_##_ret_type, \
        .arg_types = RUNTIME_FUNCTION_SIG(__VA_ARGS__) \
    }

void* runtime_resolve_function(const runtime_function_t* functions, size_t count, const char* name, wasm_type_t* type);

/** 
 * Copy the given amount of bytes from src to dst, returning false if 
 * we got any fault while doing so
 */
bool safe_copy(void* restrict dst, const void* restrict src, size_t n);
