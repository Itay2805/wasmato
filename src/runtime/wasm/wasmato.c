#include "wasmato.h"
#include "alloc/alloc.h"
#include "arch/intrin.h"
#include "lib/list.h"
#include "lib/log.h"
#include "proc.h"
#include "uapi/page.h"
#include "lib/string.h"
#include "lib/syscall.h"
#include "uapi/syscall.h"
#include "uapi/wait.h"
#include "wasm/errno.h"
#include "wasm/file.h"
#include <stdatomic.h>

uint64_t g_acpi_rsdp = -1;

static uint64_t wasmato_acpi_get_rsdp(void* memory_base, void* state_base) {
    return g_acpi_rsdp;
}

static uint64_t wasmato_map_phys(void* memory_base, void* state_base, uint64_t phys_base, uint32_t size) {
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

static void wasmato_unmap_phys(void* memory_base, void* state_base, uint32_t virt_base, uint32_t size) {
    uint64_t virt_end = ALIGN_UP((uint64_t)virt_base + size, PAGE_SIZE);
    uint64_t aligned_base = ALIGN_DOWN((uint64_t)virt_base, PAGE_SIZE);
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

/** 
 * A wrapper for a kernel handle as a file descriptor
 */
typedef struct irq_file {
    file_t file;

    /** 
     * The handle to use
     */
    uint64_t handle;
} irq_file_t;

static void irq_file_close(file_t* _file) {
    irq_file_t* file = containerof(_file, irq_file_t, file);
    sys_handle_close(file->handle);
}

static const file_ops_t m_irq_file_ops = {
    .close = irq_file_close,
};

static int wasmato_irq_create_ioapic(void* memory_base, void* state_base, uint32_t irq) {
    irq_file_t* file = mem_alloc(sizeof(*file));
    if (file == nullptr) {
        return -WASI_ERRNO_NOMEM;
    }

    // setup the file struct
    file->file.ref_count = 1;
    file->file.signals = 0;
    file->file.ops = &m_irq_file_ops;

    // create the interrupt object
    // TODO: logic to choose cpu
    wake_params_t parmas = {
        .key = &file->file.signals,
        .key_size = WAIT_KEY_UINT32,
        .mask = FILE_SIGNAL_READ_READY
    };
    uint64_t handle = sys_irq_create_ioapic(&parmas, irq, 0);
    if (handle == INVALID_HANDLE) {
        mem_free(file);
        return -1;
    }

    // save the handle
    file->handle = handle;

    // register the file, this swallows our ref
    wasm_proc_t* proc = wasm_current_proc(state_base);
    return wasm_proc_register_file(proc, &file->file);
}

static int wasmato_irq_unmask(void* memory_base, void* state_base, int fd) {
    // get the file, making sure it has the correct ops (aka type)
    wasm_proc_t* proc = wasm_current_proc(state_base);
    file_t* file = wasm_proc_get_fd(proc, fd);
    if (file->ops != &m_irq_file_ops) {
        return -WASI_ERRNO_INVAL;
    }

    // clear the read ready signal
    atomic_fetch_and_explicit(&file->signals, ~FILE_SIGNAL_READ_READY, memory_order_release);

    // call the unmask
    irq_file_t* irq = containerof(file, irq_file_t, file);
    sys_irq_unmask(irq->handle);

    // and we can put the file back
    file_put(file);

    return 0;
}

void* wasmato_resolve_import(const char* name, wasm_proc_t* proc) {
    if (proc->type == WASM_PROC_FLAG_ACPID) {
        if (strcmp(name, "acpi_get_rsdp") == 0) return wasmato_acpi_get_rsdp;
        if (strcmp(name, "map_phys") == 0) return wasmato_map_phys;
        if (strcmp(name, "unmap_phys") == 0) return wasmato_unmap_phys;
        if (strcmp(name, "io_read_8") == 0) return wasmato_io_read_8;
        if (strcmp(name, "io_read_16") == 0) return wasmato_io_read_16;
        if (strcmp(name, "io_read_32") == 0) return wasmato_io_read_32;
        if (strcmp(name, "io_write_8") == 0) return wasmato_io_write_8;
        if (strcmp(name, "io_write_16") == 0) return wasmato_io_write_16;
        if (strcmp(name, "io_write_32") == 0) return wasmato_io_write_32;
        if (strcmp(name, "io_write_32") == 0) return wasmato_io_write_32;
        if (strcmp(name, "irq_create_ioapic") == 0) return wasmato_irq_create_ioapic;
        if (strcmp(name, "irq_unmask") == 0) return wasmato_irq_unmask;
    }
    return nullptr;
}

