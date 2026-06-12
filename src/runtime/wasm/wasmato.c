#include "wasmato.h"
#include "arch/intrin.h"
#include "proc.h"
#include "uapi/page.h"
#include "lib/string.h"
#include "lib/syscall.h"

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
    }
    return nullptr;
}

