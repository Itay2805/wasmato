#pragma once

#include "lib/cpp_magic.h"
#include "lib/defs.h"
#include "lib/assert.h"
#include "wasi/wasip1.h"

typedef wasi_errno_t err_t;

/**
 * Check if there was an error
 */
#define IS_ERROR(err) (err != WASI_ERRNO_SUCCESS)

//----------------------------------------------------------------------------------------------------------------------
// A check that fails if the expression returns false
//----------------------------------------------------------------------------------------------------------------------

const char* get_error_name(err_t err);

#define CHECK_ERROR_LABEL(check, error, label, ...) \
    do { \
        if (UNLIKELY(!(check))) { \
            err = error; \
            IF(HAS_ARGS(__VA_ARGS__))(ERROR(__VA_ARGS__)); \
            ERROR("Check failed with error %s (%d) in function %s (%s:%d)", get_error_name(err), err, __FUNCTION__, __FILE__, __LINE__); \
            goto label; \
        } \
    } while(0)

#define CHECK_ERROR(check, error, ...)              CHECK_ERROR_LABEL(check, error, cleanup, ## __VA_ARGS__)
#define CHECK_LABEL(check, label, ...)              CHECK_ERROR_LABEL(check, WASI_ERRNO_INVAL, label, ## __VA_ARGS__)
#define CHECK(check, ...)                           CHECK_ERROR_LABEL(check, WASI_ERRNO_INVAL, cleanup, ## __VA_ARGS__)

#define DEBUG_CHECK_ERROR(check, error, ...)              CHECK_ERROR_LABEL(check, error, cleanup, ## __VA_ARGS__)
#define DEBUG_CHECK_LABEL(check, label, ...)              CHECK_ERROR_LABEL(check, WASI_ERRNO_INVAL, label, ## __VA_ARGS__)
#define DEBUG_CHECK(check, ...)                           CHECK_ERROR_LABEL(check, WASI_ERRNO_INVAL, cleanup, ## __VA_ARGS__)

//----------------------------------------------------------------------------------------------------------------------
// A check that fails without a condition
//----------------------------------------------------------------------------------------------------------------------

#define CHECK_FAIL(...)                             CHECK_ERROR_LABEL(0, WASI_ERRNO_INVAL, cleanup, ## __VA_ARGS__)
#define CHECK_FAIL_ERROR(error, ...)                CHECK_ERROR_LABEL(0, error, cleanup, ## __VA_ARGS__)
#define CHECK_FAIL_LABEL(label, ...)                CHECK_ERROR_LABEL(0, WASI_ERRNO_INVAL, label, ## __VA_ARGS__)
#define CHECK_FAIL_ERROR_LABEL(error, label, ...)   CHECK_ERROR_LABEL(0, error, label, ## __VA_ARGS__)

//----------------------------------------------------------------------------------------------------------------------
// A check that fails if an error was returned, used around functions returning an error
//----------------------------------------------------------------------------------------------------------------------

#define RETHROW_LABEL(error, label) \
    do { \
        err_t err__ = error; \
        if (UNLIKELY(IS_ERROR(err__))) { \
            err = err__; \
            ERROR("\trethrown at %s (%s:%d)", __FUNCTION__, __FILE__, __LINE__); \
            goto label; \
        } \
    } while(0)

#define RETHROW(error) RETHROW_LABEL(error, cleanup)
