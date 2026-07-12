#include "error.h"
#include "os.h"
#include "trace.h"
#include "uacpi/acpi.h"
#include "uacpi/tables.h"
#include <__header_poll.h>
#include <__header_unistd.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <uacpi/kernel_api.h>
#include <uacpi/status.h>
#include <uacpi/types.h>

uacpi_phys_addr g_rsdp;

__attribute__((import_module("wasmato"), import_name("acpi_get_rsdp"))) 
uint64_t wasmato_acpi_get_rsdp(void);


__attribute__((import_module("wasmato"), import_name("map_phys"))) 
void* wasmato_map_phys(uint64_t phys_base, size_t size);

__attribute__((import_module("wasmato"), import_name("unmap_phys"))) 
void wasmato_unmap_phys(void* ptr, size_t size);


__attribute__((import_module("wasmato"), import_name("io_read_8"))) 
uint8_t wasmato_io_read_8(uint16_t port);

__attribute__((import_module("wasmato"), import_name("io_read_16"))) 
uint16_t wasmato_io_read_16(uint16_t port);

__attribute__((import_module("wasmato"), import_name("io_read_32"))) 
uint32_t wasmato_io_read_32(uint16_t port);


__attribute__((import_module("wasmato"), import_name("io_write_8"))) 
void wasmato_io_write_8(uint16_t port, uint8_t value);

__attribute__((import_module("wasmato"), import_name("io_write_16"))) 
void wasmato_io_write_16(uint16_t port, uint16_t value);

__attribute__((import_module("wasmato"), import_name("io_write_32"))) 
void wasmato_io_write_32(uint16_t port, uint32_t value);


__attribute__((import_module("wasmato"), import_name("irq_create_ioapic"))) 
int wasmato_irq_create_ioapic(uint32_t irq);

__attribute__((import_module("wasmato"), import_name("irq_unmask"))) 
void wasmato_irq_unmask(int fd);

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr* out_rsdp_address) {
    uint64_t rsdp = wasmato_acpi_get_rsdp();
    if (rsdp == -1) {
        return UACPI_STATUS_NOT_FOUND;
    }
    *out_rsdp_address = rsdp;
    return UACPI_STATUS_OK;
}

uacpi_interrupt_state uacpi_kernel_disable_interrupts(void) { return 0; }
void uacpi_kernel_restore_interrupts(uacpi_interrupt_state state) {}

typedef union io_handle {
    struct {
        uint16_t base;
        uint16_t len;        
    };
    uacpi_handle handle;
} io_handle_t;
_Static_assert(sizeof(io_handle_t) <= sizeof(uacpi_handle));

uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len, uacpi_handle* out_handle) {
    // ensure this fits in 16bit, which is what x86 supports
    uacpi_io_addr top = 0;
    if (__builtin_add_overflow(base, len, &top)) return UACPI_STATUS_INVALID_ARGUMENT;
    if (len > UINT16_MAX) return UACPI_STATUS_INVALID_ARGUMENT;

    io_handle_t handle = {
        .base = base,
        .len = len
    };
    *out_handle = handle.handle;

    return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle) {}

uacpi_status uacpi_kernel_io_read8(uacpi_handle handle, uacpi_size offset, uacpi_u8 *out_value) {
    io_handle_t io = { .handle = handle };
    if (offset >= io.len) return UACPI_STATUS_INVALID_ARGUMENT;
    *out_value = wasmato_io_read_8(io.base + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read16(uacpi_handle handle, uacpi_size offset, uacpi_u16 *out_value) {
    io_handle_t io = { .handle = handle };
    if (offset >= io.len) return UACPI_STATUS_INVALID_ARGUMENT;
    *out_value = wasmato_io_read_16(io.base + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read32(uacpi_handle handle, uacpi_size offset, uacpi_u32 *out_value) {
    io_handle_t io = { .handle = handle };
    if (offset >= io.len) return UACPI_STATUS_INVALID_ARGUMENT;
    *out_value = wasmato_io_read_32(io.base + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write8(uacpi_handle handle, uacpi_size offset, uacpi_u8 in_value) {
    io_handle_t io = { .handle = handle };
    if (offset >= io.len) return UACPI_STATUS_INVALID_ARGUMENT;
    wasmato_io_write_8(io.base + offset, in_value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write16(uacpi_handle handle, uacpi_size offset, uacpi_u16 in_value) {
    io_handle_t io = { .handle = handle };
    if (offset >= io.len) return UACPI_STATUS_INVALID_ARGUMENT;
    wasmato_io_write_16(io.base + offset, in_value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write32(uacpi_handle handle, uacpi_size offset, uacpi_u32 in_value) {
    io_handle_t io = { .handle = handle };
    if (offset >= io.len) return UACPI_STATUS_INVALID_ARGUMENT;
    wasmato_io_write_32(io.base + offset, in_value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address, uacpi_handle* out_handle) {
    // TODO: use pcid instead once we get it

    // get the MCFG table
    uacpi_table table;
    uacpi_status status = uacpi_table_find_by_signature(ACPI_MCFG_SIGNATURE, &table);
    if (uacpi_unlikely_error(status)) return status;

    struct acpi_mcfg* mcfg = table.ptr;
    size_t count = mcfg->hdr.length - sizeof(*mcfg);
    uint64_t phys_addr = -1;
    for (int i = 0; i < count; i++) {
        struct acpi_mcfg_allocation* alloc = &mcfg->entries[i];

        // match the segment
        if (alloc->segment != address.segment)
            break;

        if (alloc->start_bus <= address.bus && address.bus <= alloc->end_bus) {
            // the bus matches! calculate the address
            phys_addr = alloc->address + ((address.bus - alloc->start_bus) << 20 | address.device << 15 | address.function << 12);
            break;
        }
    }

    // if we could not find the device then just give up
    if (phys_addr == -1) {
        return UACPI_STATUS_NOT_FOUND;
    }

    // map it
    void* mapping = uacpi_kernel_map(phys_addr, 4096);
    if (mapping == UACPI_MAP_FAILED) {
        return UACPI_STATUS_OUT_OF_MEMORY;
    }

    *out_handle = mapping;

    return UACPI_STATUS_OK;
}

void uacpi_kernel_pci_device_close(uacpi_handle handle) {
    uacpi_kernel_unmap(handle, 4096);
}

uacpi_status uacpi_kernel_pci_read8(uacpi_handle device, uacpi_size offset, uacpi_u8* value) {
    if (offset >= (4096 - 1)) return UACPI_STATUS_INVALID_ARGUMENT;
    *value = *(volatile uint8_t*)(device + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read16(uacpi_handle device, uacpi_size offset, uacpi_u16* value) {
    if (offset >= (4096 - 2)) return UACPI_STATUS_INVALID_ARGUMENT;
    *value = *(volatile uint16_t*)(device + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read32(uacpi_handle device, uacpi_size offset, uacpi_u32* value) {
    if (offset >= (4096 - 4)) return UACPI_STATUS_INVALID_ARGUMENT;
    *value = *(volatile uint32_t*)(device + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle device, uacpi_size offset, uacpi_u8 value) {
    if (offset >= (4096 - 1)) return UACPI_STATUS_INVALID_ARGUMENT;
    *(volatile uint8_t*)(device + offset) = value;
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write16(uacpi_handle device, uacpi_size offset, uacpi_u16 value) {
    if (offset >= (4096 - 2)) return UACPI_STATUS_INVALID_ARGUMENT;
    *(volatile uint16_t*)(device + offset) = value;
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write32(uacpi_handle device, uacpi_size offset, uacpi_u32 value) {
    if (offset >= (4096 - 4)) return UACPI_STATUS_INVALID_ARGUMENT;
    *(volatile uint32_t*)(device + offset) = value;
    return UACPI_STATUS_OK;
}

void* uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len) {
    void* res = wasmato_map_phys(addr, len);
    if (res == nullptr) {
        return UACPI_MAP_FAILED;
    }
    return res;
}

void uacpi_kernel_unmap(void *addr, uacpi_size len) {
    wasmato_unmap_phys(addr, len);
}

void* uacpi_kernel_alloc(uacpi_size size) {
    if (size == 0)
        error("attempted to allocate zero bytes");

    return malloc(size);
}

void uacpi_kernel_free(void *mem) {
    free(mem);
}


void uacpi_kernel_log(uacpi_log_level level, const uacpi_char *str) {
    switch (level) {
        default:
        case UACPI_LOG_DEBUG: printf("[?] uacpi: %s", str); break;
        case UACPI_LOG_TRACE: printf("[*] uacpi: %s", str); break;
        case UACPI_LOG_INFO: printf("[+] uacpi: %s", str); break;
        case UACPI_LOG_WARN: printf("[!] uacpi: %s", str); break;
        case UACPI_LOG_ERROR: printf("[-] uacpi: %s", str); break;
    }
}

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) {
    return get_nanosecond_timer();
}

void uacpi_kernel_stall(uacpi_u8 usec) {
    uint64_t end = get_nanosecond_timer() + (uint64_t)usec * 1000;
    while (get_nanosecond_timer() < end);
}

void uacpi_kernel_sleep(uacpi_u64 msec) {
    millisecond_sleep(msec);
}

uacpi_handle uacpi_kernel_create_mutex(void) {
    pthread_mutex_t* mutex = malloc(sizeof(*mutex));
    if (mutex == nullptr) return nullptr;

    mutex_init(mutex);
    return mutex;
}

void uacpi_kernel_free_mutex(uacpi_handle handle) {
    mutex_free(handle);
    free(handle);
}

uacpi_thread_id uacpi_kernel_get_thread_id(void) {
    return get_thread_id();
}

uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle handle, uacpi_u16 timeout) {
    if (timeout == 0)
        return mutex_try_lock(handle) ? UACPI_STATUS_OK : UACPI_STATUS_TIMEOUT;

    if (timeout == 0xFFFF) {
        mutex_lock(handle);
        return UACPI_STATUS_OK;
    }

    if (mutex_lock_timeout(handle, timeout * 1000000ull))
        return UACPI_STATUS_OK;

    return UACPI_STATUS_TIMEOUT;
}

void uacpi_kernel_release_mutex(uacpi_handle handle) {
    mutex_unlock(handle);
}

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condvar;
    size_t counter;
} event_t;

uacpi_handle uacpi_kernel_create_event(void) {
    event_t* event = calloc(1, sizeof(*event));
    if (event == nullptr) return nullptr;

    mutex_init(&event->mutex);
    condvar_init(&event->condvar);
    return event;
}

void uacpi_kernel_free_event(uacpi_handle handle) {
    event_t* event = handle;
    condvar_free(&event->condvar);
    mutex_free(&event->mutex);
    free(handle);
}

static bool event_pred(void *ptr) {
    event_t* event = ptr;
    return event->counter != 0;
}

uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle handle, uacpi_u16 timeout) {
    event_t* event = handle;

    mutex_lock(&event->mutex);

    if (event->counter > 0) {
        event->counter -= 1;
        mutex_unlock(&event->mutex);
        return UACPI_TRUE;
    }

    if (timeout == 0) {
        mutex_unlock(&event->mutex);
        return UACPI_FALSE;
    }

    if (timeout == 0xFFFF) {
        condvar_wait(&event->condvar, &event->mutex, event_pred, event);

        event->counter -= 1;
        mutex_unlock(&event->mutex);
        return UACPI_TRUE;
    }

    bool ok = condvar_wait_timeout(
        &event->condvar, &event->mutex, 
        event_pred, event, 
        timeout * 1000000ull
    );
    if (ok)
        event->counter -= 1;

    mutex_unlock(&event->mutex);
    return ok ? UACPI_TRUE : UACPI_FALSE;
}

void uacpi_kernel_signal_event(uacpi_handle handle) {
    event_t* event = handle;

    mutex_lock(&event->mutex);

    event->counter += 1;
    condvar_signal(&event->condvar);

    mutex_unlock(&event->mutex);
}

void uacpi_kernel_reset_event(uacpi_handle handle) {
    event_t* event = handle;

    mutex_lock(&event->mutex);

    event->counter = 0;

    mutex_unlock(&event->mutex);
}

uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *req) {
    switch (req->type) {
        case UACPI_FIRMWARE_REQUEST_TYPE_BREAKPOINT: {
            ERROR("acpid: Ignoring breakpoint");
        } break;

        case UACPI_FIRMWARE_REQUEST_TYPE_FATAL: {
            ERROR("acpid: Fatal firmware error: type: %x code: %x arg: %llx", 
                req->fatal.type, req->fatal.code, req->fatal.arg);
        } break;

        default: {
            error("acpid: unknown firmware request type %d", req->type);
        } break;
    }

    return UACPI_STATUS_OK;
}

typedef struct interrupt_handler {
    uacpi_interrupt_handler handler;
    uacpi_handle ctx;
    pthread_t thread;
    int fd;
} interrupt_handler_t;

static void* interrupt_thread(void* ctx) {
    interrupt_handler_t* intr = ctx;

    struct pollfd intrfd = {
        .fd = intr->fd,
        .events = POLLIN
    };

    while (intr->fd >= 0) {
        // poll the fd to wait for an interrupt
        intrfd.revents = 0;
        int ready = poll(&intrfd, 1, -1);
        if (ready < 0)
            break;

        if (intrfd.revents & POLLNVAL)
            break;

        if (intrfd.revents & POLLIN) {
            // run the handler
            intr->handler(intr->ctx);

            // tell the runtime we finished handling the event
            wasmato_irq_unmask(intr->fd);
        }        
    }

    return NULL;
}

uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq, uacpi_interrupt_handler handler, uacpi_handle ctx, uacpi_handle* out_irq_handle) {
    // request an interrupt from the kernel
    int fd = wasmato_irq_create_ioapic(irq);
    if (fd < 0) {
        ERROR("Failed to create IRQ for #%d", irq);
        return UACPI_STATUS_INTERNAL_ERROR;
    }

    // setup the struct
    interrupt_handler_t* handle = malloc(sizeof(interrupt_handler_t));
    if (handle == nullptr) {
        close(fd);
        return UACPI_STATUS_OUT_OF_MEMORY;
    }
    handle->handler = handler;
    handle->ctx = ctx;
    handle->fd = fd;

    // start the thread
    if (pthread_create(&handle->thread, NULL, interrupt_thread, handle)) {
        return UACPI_STATUS_INTERNAL_ERROR;
    }

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler handler, uacpi_handle irq_handle) {
    printf("TODO: uacpi_kernel_uninstall_interrupt_handler\n");
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_handle uacpi_kernel_create_spinlock(void) {
    return uacpi_kernel_create_mutex();
}

void uacpi_kernel_free_spinlock(uacpi_handle handle) {
    uacpi_kernel_free_mutex(handle);
}

uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle handle) {
    uacpi_kernel_acquire_mutex(handle, 0xFFFF);
    return 0;
}

void uacpi_kernel_unlock_spinlock(uacpi_handle handle, uacpi_cpu_flags flags) {
    uacpi_kernel_release_mutex(handle);
}

uacpi_status uacpi_kernel_schedule_work(uacpi_work_type type, uacpi_work_handler handler, uacpi_handle ctx) {
    // TODO: is this correct?
    handler(ctx);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_wait_for_work_completion(void) {
    return UACPI_STATUS_OK;
}
