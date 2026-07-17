#pragma once

#include "lib/except.h"
#include "wasi/wasip1.h"
#include "wasm/error.h"

//----------------------------------------------------------------------------------------------------------------------
// A check that fails if an error was returned, used around functions returning an error
//----------------------------------------------------------------------------------------------------------------------

static inline err_t wasm_err(wasm_err_t err) {
    switch (err) {
        case WASM_NO_ERROR: return WASI_ERRNO_SUCCESS;
        case WASM_ERROR_CHECK_FAILED: return WASI_ERRNO_INVAL;
        default: return WASI_ERRNO_INVAL;
    }
}

#define RETHROW_WASM_LABEL(error, label) \
    do { \
        err = wasm_err(error); \
        if (UNLIKELY(IS_ERROR(err))) { \
            ERROR("\trethrown at %s (%s:%d)", __FUNCTION__, __FILE__, __LINE__); \
            goto label; \
        } \
    } while(0)

#define RETHROW_WASM(error) RETHROW_WASM_LABEL(error, cleanup)

