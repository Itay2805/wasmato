#include "error.h"
#include "os.h"
#include "trace.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uacpi/kernel_api.h>
#include <uacpi/status.h>
#include <uacpi/types.h>

uacpi_phys_addr g_rsdp;

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr* out_rsdp_address) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_interrupt_state uacpi_kernel_disable_interrupts(void) { return 0; }
void uacpi_kernel_restore_interrupts(uacpi_interrupt_state state) {}

uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len, uacpi_handle* out_handle) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

void uacpi_kernel_io_unmap(uacpi_handle handle) {}

#define UACPI_IO_READ(bits)                                                \
    uacpi_status uacpi_kernel_io_read##bits(                               \
        uacpi_handle handle, uacpi_size offset, uacpi_u##bits *out_value   \
    )                                                                      \
    {                                                                      \
        return UACPI_STATUS_UNIMPLEMENTED;                                 \
    }

#define UACPI_IO_WRITE(bits)                                              \
    uacpi_status uacpi_kernel_io_write##bits(                             \
        uacpi_handle handle, uacpi_size offset, uacpi_u##bits in_value    \
    )                                                                     \
    {                                                                     \
        return UACPI_STATUS_UNIMPLEMENTED;                                \
    }

#define UACPI_PCI_READ(bits)                                         \
    uacpi_status uacpi_kernel_pci_read##bits(                        \
        uacpi_handle handle, uacpi_size offset, uacpi_u##bits *value \
    )                                                                \
    {                                                                \
        return UACPI_STATUS_UNIMPLEMENTED;                           \
    }

#define UACPI_PCI_WRITE(bits)                                       \
    uacpi_status uacpi_kernel_pci_write##bits(                      \
        uacpi_handle handle, uacpi_size offset, uacpi_u##bits value \
    )                                                               \
    {                                                               \
        return UACPI_STATUS_UNIMPLEMENTED;                          \
    }

UACPI_IO_READ(8)
UACPI_IO_READ(16)
UACPI_IO_READ(32)

UACPI_IO_WRITE(8)
UACPI_IO_WRITE(16)
UACPI_IO_WRITE(32)

UACPI_PCI_READ(8)
UACPI_PCI_READ(16)
UACPI_PCI_READ(32)

UACPI_PCI_WRITE(8)
UACPI_PCI_WRITE(16)
UACPI_PCI_WRITE(32)

uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address, uacpi_handle* out_handle) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

void uacpi_kernel_pci_device_close(uacpi_handle handle) {
}

void* uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len) {
    return nullptr;
}

void uacpi_kernel_unmap(void *addr, uacpi_size len) {
}

void *uacpi_kernel_alloc(uacpi_size size) {
    if (size == 0)
        error("attempted to allocate zero bytes");

    return malloc(size);
}

void uacpi_kernel_free(void *mem) {
    free(mem);
}


void uacpi_kernel_log(uacpi_log_level level, const uacpi_char *str)
{
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

uacpi_handle uacpi_kernel_create_event(void)
{
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

uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq, uacpi_interrupt_handler handler, uacpi_handle ctx, uacpi_handle* out_irq_handle) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler handler, uacpi_handle irq_handle) {
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
