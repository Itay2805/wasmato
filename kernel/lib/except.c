#include "except.h"

const char* get_error_code(err_t err) {
    switch (err) {
        case NO_ERROR: return "NO_ERROR";
        case ERROR_CHECK_FAILED: return "ERROR_CHECK_FAILED";
        case ERROR_OUT_OF_MEMORY: return "ERROR_OUT_OF_MEMORY";
        case ERROR_NOT_FOUND: return "ERROR_NOT_FOUND";
        default: return "Unknown Error";
    }
}
