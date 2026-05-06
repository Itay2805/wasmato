#include <stdarg.h>

#include "wasm/host.h"

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#include "alloc/alloc.h"
#include "lib/log.h"
#include "lib/stb_sprintf.h"
#include "lib/syscall.h"
#include "uapi/page.h"

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

size_t wasm_host_page_size(void) {
    return PAGE_SIZE;
}

void wasm_host_snprintf(char* buffer, size_t len, const char* fmt, ...) {
    va_list ap = {};
    va_start(ap, fmt);
    stbsp_vsnprintf(buffer, len, fmt, ap);
    va_end(ap);
}

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

int32_t wasm_host_memory_size(void* memory_base) {
    return 0;
}

int32_t wasm_host_memory_grow(void* memory_base, int32_t new_page_count) {
    return -1;
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
