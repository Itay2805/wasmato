#include "syscall.h"
#include "mem/vmar.h"
#include "uapi/syscall.h"

#include <stdatomic.h>
#include <stdint.h>

#include "limine_requests.h"
#include "runtime.h"
#include "arch/apic.h"
#include "arch/gdt.h"
#include "arch/intrin.h"
#include "arch/regs.h"
#include "lib/ipi.h"
#include "lib/pcpu.h"
#include "lib/rbtree/rbtree.h"
#include "mem/mappings.h"
#include "mem/phys.h"
#include "mem/virt.h"
#include "thread/sched.h"
#include "thread/wait.h"

/**
 * are we done with early memory
 */
LATE_RO static bool m_early_done = false;

static void assert_user_range(uintptr_t addr, size_t size) {
    uintptr_t end;
    ASSERT(addr >= (uintptr_t)g_user_memory.base);
    ASSERT(!__builtin_add_overflow(addr, size, &end));
    ASSERT(end <= (uintptr_t)vmar_end(&g_user_memory));
}

static void copy_string_from_user(void* dst, uintptr_t src, size_t max_size) {
    user_access_enable();

    // copy over the chars
    for (size_t i = 0; i < max_size - 1; i++, dst++, src++) {
        // ensure that the source is still valid
        assert_user_range(src, 1);
        char value = *(char*)src;
        if (value == '\0') {
            break;
        }
        *(char*)dst = value;
    }

    // ensure we have null-terminator
    *(char*)dst = '\0';

    user_access_disable();
}

static void copy_to_user(uintptr_t dst, void* src, size_t size) {
    assert_user_range(dst, size);

    user_access_enable();
    memcpy((void*)dst, src, size);
    user_access_disable();
}

INIT_CODE static err_t handle_early_done(void) {
    err_t err = NO_ERROR;

    // ensure all cores have the scheduler
    // enabled properly before we start
    ipi_broadcast(IPI_SYNC_EARLY_DONE);

    // ensure we are not done yet
    CHECK(!m_early_done);
    m_early_done = true;

    // we don't need the bootloader memory anymore
    RETHROW(reclaim_bootloader_memory());

    // reprotect data that should be read-only
    protect_ro_data();

cleanup:
    return err;
}

static err_t handle_jit_alloc(size_t rx_page_count, size_t ro_page_count, void** ptr) {
    err_t err = NO_ERROR;

    vmar_lock();

    // must have at least one code page
    CHECK(rx_page_count != 0);

    // ensure the two don't overflow
    size_t total_pages = 0;
    CHECK(!__builtin_add_overflow(rx_page_count, ro_page_count, &total_pages));

    // reserve a range for everything, we
    // want it close together
    vmar_t* jit_vmar = vmar_reserve(&g_user_code_region, total_pages, nullptr);
    CHECK_ERROR(jit_vmar != nullptr, ERROR_OUT_OF_MEMORY);
    snprintf(jit_vmar->name, sizeof(jit_vmar->name), "jit");

    // setup rx pages if any
    vmar_t* rx_vmar = vmar_allocate(jit_vmar, rx_page_count, jit_vmar->base);
    CHECK_ERROR(rx_vmar != nullptr, ERROR_OUT_OF_MEMORY);
    snprintf(rx_vmar->name, sizeof(rx_vmar->name), "code");

    // setup ro pages if any
    if (ro_page_count != 0) {
        vmar_t* ro_vmar = vmar_allocate(jit_vmar, ro_page_count, vmar_end(rx_vmar) + 1);
        CHECK_ERROR(ro_vmar != nullptr, ERROR_OUT_OF_MEMORY);
        snprintf(ro_vmar->name, sizeof(ro_vmar->name), "constpool");
    }

    // don't allow to modify the top level vmar
    jit_vmar->locked = true;

    // output it
    *ptr = jit_vmar->base;

cleanup:
    if (IS_ERROR(err) && jit_vmar != nullptr) {
        vmar_free(jit_vmar);
    }

    vmar_unlock();

    return err;
}

static err_t handle_jit_lock_protection(void* ptr) {
    err_t err = NO_ERROR;

    vmar_lock();

    // get the first region
    vmar_t* rx_region = vmar_find_mapping(&g_user_code_region, ptr);
    CHECK(rx_region != nullptr);

    // set the code
    CHECK(rx_region->parent->base == rx_region->base);
    vmar_protect(rx_region, MAPPING_PROTECTION_RX);

    // set constpool
    struct rb_node* next = rb_next(&rx_region->node);
    if (next != nullptr) {
        CHECK(next != nullptr);
        vmar_t* ro_region = rb_entry(next, vmar_t, node);
        vmar_protect(ro_region, MAPPING_PROTECTION_RO);
    }

cleanup:
    vmar_unlock();

    return err;
}

OMIT_ENDBR uint64_t syscall_handler(uint64_t syscall, uint64_t arg1, uint64_t arg2, uint64_t rip, uint64_t arg3, uint64_t arg4) {
    err_t err = NO_ERROR;
    uint64_t result = 0;

    switch (syscall) {
        case SYSCALL_DEBUG_PRINT: {
            assert_user_range(arg1, arg2);
            user_access_enable();
            debug_print_raw((const char*)arg1, arg2);
            user_access_disable();
        } break;

        case SYSCALL_HEAP_ALLOC: {
            vmar_lock();
            vmar_t* region = vmar_allocate(&g_user_memory, arg1, nullptr);
            if (region != nullptr) {
                vmar_set_name(region, "heap");
            }
            vmar_unlock();

            if (region == NULL) {
                result = 0;
            } else {
                result = (uintptr_t)region->base;
            }
        } break;

        case SYSCALL_HEAP_FREE: {
            vmar_lock();
            vmar_t* mapping = vmar_find_mapping(&g_user_memory, (void*)arg1);
            CHECK(mapping != nullptr);
            CHECK(mapping->parent == &g_user_memory);
            CHECK(mapping->type == VMAR_TYPE_ALLOC);
            CHECK(mapping->alloc.protection == MAPPING_PROTECTION_RW);
            vmar_free(mapping);
            vmar_unlock();
        } break;

        case SYSCALL_JIT_ALLOC: {
            void* ptr = nullptr;
            err = handle_jit_alloc(arg1, arg2, &ptr);
            if (err == ERROR_OUT_OF_MEMORY) {
                result = 0;
            } else if (!IS_ERROR(err)) {
                result = (uintptr_t)ptr;
            } else {
                RETHROW(err);
            }
        } break;

        case SYSCALL_JIT_LOCK_PROTECTION: {
            handle_jit_lock_protection((void*)arg1);
        } break;

        case SYSCALL_JIT_FREE: {
            vmar_lock();
            vmar_t* region = vmar_find_mapping(&g_user_code_region, (void*)arg1);
            CHECK(region != nullptr);
            vmar_free(region);
            vmar_unlock();
        } break;

        case SYSCALL_THREAD_CREATE: {
            // create the new thread
            thread_t* thread = thread_create(
                runtime_thread_entry_thunk,
                (void*)arg1,
                THREAD_FLAG_USER,
                ""
            );

            // fail if the thread creation failed
            if (thread == nullptr) {
                result = false;
            } else {
                result = true;
            }

            // copy the name from the user
            copy_string_from_user(thread->name, arg2, sizeof(thread->name));

            // start the thread
            thread_start(thread);
        } break;

        case SYSCALL_THREAD_SLEEP: {
            thread_sleep(arg1);
        } break;

        case SYSCALL_THREAD_EXIT: {
            thread_exit();
        } break;

        case SYSCALL_ATOMIC_WAIT32: {
            assert_user_range(arg1, sizeof(uint32_t));
            atomic_wait((void*)arg1, WAIT_KEY_UINT32, arg2, arg3);
        } break;

        case SYSCALL_ATOMIC_WAIT64: {
            assert_user_range(arg1, sizeof(uint64_t));
            atomic_wait((void*)arg1, WAIT_KEY_UINT64, arg2, arg3);
        } break;

        case SYSCALL_ATOMIC_NOTIFY: {
            assert_user_range(arg1, sizeof(uint32_t));
            result = atomic_notify((void*)arg1, arg2);
        } break;

        case SYSCALL_EARLY_GET_INITRD_SIZE: {
            CHECK(!m_early_done);
            struct limine_file* file = g_limine_module_request.response->modules[0];
            result = file->size;
        } break;

        case SYSCALL_EARLY_GET_INITRD: {
            CHECK(!m_early_done);
            struct limine_file* file = g_limine_module_request.response->modules[0];
            copy_to_user(arg1, file->address, file->size);
        } break;

        case SYSCALL_EARLY_DONE: {
            // perform last cleanups and reclaim all init code
            RETHROW(handle_early_done());
            reclaim_init_mem();
        } break;

        default:
            ASSERT(false, "syscall: Unknown syscall: %ld", syscall);
    }

cleanup:
    ASSERT(!IS_ERROR(err), "syscall: error while performing syscall");

    return result;
}

// this is called directly by the stub and no-one else

void syscall_entry(void);

INIT_CODE void init_syscall() {
    // setup the main descriptors
    __wrmsr(MSR_IA32_STAR, (GDT_KERNEL_CODE << 32) | ((GDT_USER_CODE - 16) << 48));

    // setup the entry points
    __wrmsr(MSR_IA32_LSTAR, (uintptr_t)syscall_entry);
    __wrmsr(MSR_IA32_CSTAR, 0);

    // mask basically all the flags we can
    rflags_t flags = {
        .CF = 1,
        .PF = 1,
        .AF = 1,
        .ZF = 1,
        .SF = 1,
        .TF = 1,
        .IF = 1,
        .DF = 1,
        .OF = 1,
        .IOPL = 0b11,
        .NT = 1,
        .RF = 1,
        .AC = 1,
        .ID = 1,
    };
    __wrmsr(MSR_IA32_FMASK, flags.packed);
}
