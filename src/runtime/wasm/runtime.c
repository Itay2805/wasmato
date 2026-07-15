#include "lib/log.h"
#include "lib/syscall.h"

#include "uapi/page.h"

#include <stdint.h>
#include <wasm/wasm.h>
#include <wasm/host.h>

#include "alloc/alloc.h"
#include "lib/stb_sprintf.h"
#include "lib/tsc.h"
#include "uapi/wait.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Host logging
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static char* debug_print_cb(const char* buf, void* user, int len) {
    sys_debug_print(buf, len);
    return user;
}

void wasm_host_log(wasm_host_log_level_t log_level, const char* fmt, ...) {
    switch (log_level) {
        case WASM_HOST_LOG_RAW: break;
        case WASM_HOST_LOG_DEBUG: sys_debug_print("[?] wasm: ", 10); break;
        case WASM_HOST_LOG_TRACE: sys_debug_print("[*] wasm: ", 10); break;
        case WASM_HOST_LOG_WARN: sys_debug_print("[!] wasm: ", 10); break;
        case WASM_HOST_LOG_ERROR: sys_debug_print("[-] wasm: ", 10); break;
    }

    char buffer[STB_SPRINTF_MIN];
    va_list ap = {};
    va_start(ap, fmt);
    stbsp_vsprintfcb(debug_print_cb, buffer, buffer, fmt, ap);
    va_end(ap);

    if (log_level != WASM_HOST_LOG_RAW) {
        sys_debug_print("\n", 1);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Host jit interface
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

size_t wasm_host_page_size(void) {
    return PAGE_SIZE;
}

void* wasm_host_jit_alloc(size_t rx_page_count, size_t ro_page_count) {
    return sys_jit_alloc(rx_page_count, ro_page_count);
}

bool wasm_host_jit_lock(void* ptr, size_t rx_page_count, size_t ro_page_count) {
    sys_jit_lock_protection(ptr);
    return true;
}

void wasm_host_jit_free(void* ptr, size_t rx_page_count, size_t ro_page_count) {
    sys_jit_free(ptr);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Runtime memory allocation
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* wasm_host_calloc(size_t nmemb, size_t size) {
    size *= nmemb;
    void* ptr = mem_alloc(size);
    if (ptr != nullptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void* wasm_host_realloc(void* ptr, size_t new_size) {
    return mem_realloc(ptr, new_size);
}

void wasm_host_free(void* ptr) {
    return mem_free(ptr);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// WASM atomic handling
//
// wait on the given address, the possible return values are:
// 0 - "ok", woken by another agent in the cluster
// 1 - "not-equal", the loaded value did not match the expected value
// 2 - "timed-out", not woken before timeout expired
//
// The timeout is in relative nanoseconds, and if its a negative
// number it should not have any timeout
//
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t wasm_host_atomic_notify(void* ptr, uint32_t count) {
    // for the kernel 0 means wake all, for wasm it means not waking 
    // up any threads, so just pass if that happens
    if (count == 0) {
        return 0;
    }
    return sys_atomic_notify(ptr, UINT64_MAX, count);
}

static uint64_t wasm_atomic_deadline(int64_t timeout) {
    if (timeout <= 0) {
        return (uint64_t)-1;
    }
    return tsc_ns_deadline((uint64_t)timeout);
}

static uint32_t wasm_atomic_woken_result(uint64_t deadline) {
    if (tsc_check_deadline(deadline)) {
        return 2; // "not-equal"
    }
    return 0; // "ok"
}

uint32_t wasm_host_atomic_wait_4(_Atomic(uint32_t)* value, uint32_t expected, int64_t timeout) {
    uint64_t deadline = wasm_atomic_deadline(timeout);

    wait_entry_t entry = {
        .key = value,
        .key_size = WAIT_KEY_UINT32,
        .old = expected,
        .mask = UINT64_MAX
    };
    if (sys_atomic_wait(&entry, 1, deadline) == WAIT_STATUS_NOT_EQUAL) {
        return 1; // "not-equal"
    }

    return wasm_atomic_woken_result(deadline);
}

uint32_t wasm_host_atomic_wait_8(_Atomic(uint64_t)* value, uint64_t expected, int64_t timeout) {
    uint64_t deadline = wasm_atomic_deadline(timeout);

    wait_entry_t entry = {
        .key = value,
        .key_size = WAIT_KEY_UINT64,
        .old = expected,
        .mask = UINT64_MAX
    };
    if (sys_atomic_wait(&entry, 1, deadline) == WAIT_STATUS_NOT_EQUAL) {
        return 1; // "not-equal"
    }
    
    return wasm_atomic_woken_result(deadline);
}