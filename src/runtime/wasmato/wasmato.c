#include "wasmato.h"
#include "alloc/alloc.h"
#include "arch/intrin.h"
#include "lib/assert.h"
#include "lib/defs.h"
#include "lib/except.h"
#include "lib/list.h"
#include "lib/syscall.h"
#include "proc/handle.h"
#include "proc/object.h"
#include "proc/proc.h"
#include "runtime.h"
#include "uapi/page.h"
#include "uapi/syscall.h"
#include "wasi/wasi.h"
#include "wasi/wasip1.h"
#include <stdint.h>

typedef struct kernel_object {
    object_t object;

    /** 
     * The handle to the kernel object
     */
    uint64_t kernel_handle;
} kernel_object_t;

static void wasmato_kernel_object_free(object_t* obj) {
    kernel_object_t* ko = containerof(obj, kernel_object_t, object);
    sys_handle_close(ko->kernel_handle);
}

uint64_t g_acpi_rsdp = -1;

static uint64_t wasmato_acpi_get_rsdp(void* memory_base, void* state_base) {
    ASSERT(g_acpi_rsdp != -1);
    uint64_t value = g_acpi_rsdp;
    g_acpi_rsdp = -1;
    return value;
}

static wasi_ptr_t wasmato_map_phys(void* memory_base, void* state_base, uint64_t phys_base, wasi_size_t size) {
    uint64_t phys_end;
    if (__builtin_add_overflow(phys_base, size, &phys_end)) {
        return 0;
    }
    phys_end = ALIGN_UP(phys_end, PAGE_SIZE);
    uint64_t aligned_base = ALIGN_DOWN(phys_base, PAGE_SIZE);
    size_t page_count = (phys_end - aligned_base) / PAGE_SIZE;

    uint32_t wasm_addr = sys_mem_map_phys(memory_base, aligned_base, page_count) - memory_base;
    return wasm_addr + (phys_base - aligned_base);
}

static void wasmato_unmap_phys(void* memory_base, void* state_base, wasi_ptr_t ptr, wasi_size_t size) {
    uint64_t virt_end = ALIGN_UP((uint64_t)ptr + size, PAGE_SIZE);
    uint64_t aligned_base = ALIGN_DOWN((uint64_t)ptr, PAGE_SIZE);
    size_t page_count = (virt_end - aligned_base) / PAGE_SIZE;

    void* aligned_ptr = memory_base + aligned_base;
    sys_mem_unmap_phys(aligned_ptr, page_count);
}

static uint8_t wasmato_io_read_8(void* memory_base, void* state_base, uint16_t port) {
    return __inbyte(port);
}

static uint16_t wasmato_io_read_16(void* memory_base, void* state_base, uint16_t port) {
    return __inword(port);
}

static uint32_t wasmato_io_read_32(void* memory_base, void* state_base, uint16_t port) {
    return __indword(port);
}

static void wasmato_io_write_8(void* memory_base, void* state_base, uint16_t port, uint8_t value) {
    __outbyte(port, value);
}

static void wasmato_io_write_16(void* memory_base, void* state_base, uint16_t port, uint16_t value) {
    __outword(port, value);
}

static void wasmato_io_write_32(void* memory_base, void* state_base, uint16_t port, uint32_t value) {
    __outdword(port, value);
}

static wasi_fd_t wasmato_irq_create_ioapic(void* memory_base, void* state_base, uint32_t irq) {
    kernel_object_t* obj = mem_alloc(sizeof(*obj));
    if (obj == nullptr) {
        return -1;
    }

    // create the irq object, making it turn the object to be writable,
    // aka, we can ack it
    wake_params_t parmas = {
        .key = &obj->object.signals,
        .key_size = WAIT_KEY_UINT32,
        .mask = SIGNAL_WRITABLE
    };
    uint64_t handle = sys_irq_create_ioapic(&parmas, irq, 0);
    if (handle == INVALID_HANDLE) {
        mem_free(obj);
        return -1;
    }

    // finalize the object setup
    object_init(&obj->object);
    obj->object.type = OBJECT_TYPE_INTERRUPT;
    obj->object.free = wasmato_kernel_object_free;

    // save the kernel handle itself
    obj->kernel_handle = handle;

    // The rights:
    // - wait (required for poll)
    // - write (required for ack)
    rights_t rights = RIGHT_WAIT | RIGHT_WRITE;

    // and finally create the handle itself
    wasm_proc_t* proc = wasm_current_proc(state_base);
    return handle_table_allocate(
        &proc->handles, 
        &obj->object, 
        rights        
    );
}

static wasi_errno_t wasmato_irq_unmask(void* memory_base, void* state_base, wasi_fd_t fd) {
    wasi_errno_t err = WASI_ERRNO_SUCCESS;
    
    wasm_proc_t* proc = wasm_current_proc(state_base);
    handle_t handle = handle_table_lookup(&proc->handles, fd);
    if (handle.object == nullptr) {
        return WASI_ERRNO_BADF;
    }

    // ensure this is an interrupt object and that 
    // the handle has a write permission
    CHECK_ERROR(handle.object->type == OBJECT_TYPE_INTERRUPT, WASI_ERRNO_INVAL);
    CHECK_ERROR(handle.rights & RIGHT_WRITE, WASI_ERRNO_NOTCAPABLE);

    // perform the actual operation
    kernel_object_t* ko = containerof(handle.object, kernel_object_t, object);

    // we are about to unmask it, clear the signal
    object_clear_signal(&ko->object, SIGNAL_WRITABLE);

    // and actually clear it
    sys_irq_unmask(ko->kernel_handle);

cleanup:
    object_put(handle.object);
    return err;
}

static const runtime_function_t m_wasmato_acpid_functions[] = {
    RUNTIME_FUNCTION(wasmato, acpi_get_rsdp, I64),

    RUNTIME_FUNCTION(wasmato, map_phys, I32, I64, I32),
    RUNTIME_FUNCTION(wasmato, unmap_phys, INVALID, I32, I32),
    
    RUNTIME_FUNCTION(wasmato, io_read_8, I32, I32),
    RUNTIME_FUNCTION(wasmato, io_read_16, I32, I32),
    RUNTIME_FUNCTION(wasmato, io_read_32, I32, I32),

    RUNTIME_FUNCTION(wasmato, io_write_8, INVALID, I32, I32),
    RUNTIME_FUNCTION(wasmato, io_write_16, INVALID, I32, I32),
    RUNTIME_FUNCTION(wasmato, io_write_32, INVALID, I32, I32),

    RUNTIME_FUNCTION(wasmato, irq_create_ioapic, I32, I32),
    RUNTIME_FUNCTION(wasmato, irq_unmask, INVALID, I32),
};

void* wasmato_resolve_import(const char* name, wasm_proc_t* proc, wasm_type_t* type) {
    if (proc->type == WASM_PROC_TYPE_ACPID) {
        return runtime_resolve_function(
            m_wasmato_acpid_functions, 
            ARRAY_LENGTH(m_wasmato_acpid_functions), 
            name, type
        );
    }
    return nullptr;
}
