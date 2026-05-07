#include "wasm.h"

#include "wasm/host.h"

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdnoreturn.h>

#include "alloc/alloc.h"
#include "lib/except.h"
#include "lib/log.h"
#include "lib/stb_sprintf.h"
#include "lib/syscall.h"
#include "uapi/page.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Host runtime
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void wasm_entry_point(wasm_entry_args_t* args) {
    err_t err = NO_ERROR;
    void* memory_base = nullptr;

    // prepare the state
    void* state_base = nullptr;
    if (args->jit.state_size != 0) {
        state_base = mem_alloc(args->jit.state_size);
        CHECK(state_base != nullptr);
        if (args->jit.state_init) {
            memcpy(state_base, args->jit.state_init, args->jit.state_size);
        }
    }

    // prepare the memory region, the entire region is 8gb to ensure full 33bit
    // access without any need for length checks
    memory_base = sys_mem_reserve(SIZE_TO_PAGES(SIZE_8GB), "wasm-memory");
    CHECK(memory_base != nullptr);

    // prepare the minimal bump
    CHECK(sys_mem_bump(memory_base, SIZE_TO_PAGES(args->module.memory_min)) != nullptr);

    // copy all the data segments that are needed on start
    for (int64_t i = 0; i < args->module.data_segments_count; i++) {
        wasm_data_segment_t* segment = &args->module.data_segments[i];
        memcpy(memory_base + segment->offset, segment->data, segment->len);
    }

    // find the entry point
    int64_t export = wasm_find_export(&args->module, "_start");
    CHECK(export >= 0);
    CHECK(args->module.exports[export].kind == WASM_EXPORT_FUNC);

    // validate it has an empty signature
    uint32_t funcidx = args->module.exports[export].index;
    CHECK(funcidx >= args->module.imports_count);
    funcidx -= args->module.imports_count;
    typeidx_t type = args->module.functions[funcidx];
    CHECK(args->module.types[type].arg_types_count == 0);

    // and now we can call it
    void (*start)(void* memory_base, void* state_base) = args->jit.exports[export].func.address;
    start(memory_base, state_base);

cleanup:
    mem_free(state_base);

    if (memory_base != nullptr) {
        sys_mem_free(memory_base);
    }

    (void)err;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Host functions required by the wasm runtime
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
    ASSERT(!"TODO: wasm_host_memory_size");
    return 0;
}

int32_t wasm_host_memory_grow(void* memory_base, int32_t new_page_count) {
    ASSERT(!"TODO: wasm_host_memory_grow");
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
