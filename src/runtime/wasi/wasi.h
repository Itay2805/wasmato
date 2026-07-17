#pragma once

#include "wasm/wasm.h"

#define WASI_CHECK(check, _errno) \
    do { \
        if (UNLIKELY(!(check))) { \
            errno = WASI_ERRNO_##_errno; \
            ERROR("wasi: Check failed with %s at %s (%s:%d)", #_errno, __FUNCTION__, __FILE__, __LINE__); \
            goto cleanup; \
        } \
    } while(0)

void* wasi_resolve_import(const char* name, wasm_type_t* type);
