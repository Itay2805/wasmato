#pragma once
#include "lib/except.h"

typedef enum wasm_type_kind {
    WASM_TYPE_KIND_FUNC,
} wasm_type_kind_t;

typedef enum wasm_value_type {
    // Number Types
    WASM_VALUE_TYPE_F64,
    WASM_VALUE_TYPE_F32,
    WASM_VALUE_TYPE_I64,
    WASM_VALUE_TYPE_I32,
} wasm_value_type_t;

typedef struct wasm_value {
    wasm_value_type_t kind;
    union {
        int32_t i32;
        int64_t i64;
        float f32;
        double f64;
    } value;
} wasm_value_t;

typedef struct wasm_type {
    wasm_type_kind_t kind;
    union {
        struct {
            wasm_value_type_t* arg_types;
            wasm_value_type_t* result_types;
        } func;
    };
} wasm_type_t;

typedef struct wasm_global {
    wasm_value_t value;
    bool mutable;
} wasm_global_t;

typedef enum wasm_export_kind {
    WASM_EXPORT_FUNC,
    WASM_EXPORT_TABLE,
    WASM_EXPORT_MEMORY,
    WASM_EXPORT_GLOBAL,
    WASM_EXPORT_TAG,
} wasm_export_kind_t;

typedef struct wasm_export {
    char* key;
    wasm_export_kind_t kind;
    uint32_t index;
} wasm_export_t;

typedef struct wasm_code {
    void* code;
    uint32_t length;
} wasm_code_t;

typedef uint32_t typeidx_t;

typedef struct wasm_module {
    wasm_type_t* types;
    typeidx_t* functions;
    wasm_global_t* globals;
    wasm_export_t* exports;
    wasm_code_t* code;

    uint64_t memory_min;
    uint64_t memory_max;
} wasm_module_t;

err_t wasm_load_module(wasm_module_t* module, void* data, size_t size);

/**
 * Find an export in the module, returns null if not found
 */
wasm_export_t* wasm_find_export(wasm_module_t* module, const char* name);

/**
 * Free the contents of the given module
 */
void wasm_module_free(wasm_module_t* module);
