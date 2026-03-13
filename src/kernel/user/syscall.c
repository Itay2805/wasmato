#include "syscall.h"
#include "uapi/syscall.h"

#include <stdatomic.h>

#include "stack.h"
#include "arch/apic.h"
#include "arch/gdt.h"
#include "arch/intr.h"
#include "arch/intrin.h"
#include "arch/regs.h"
#include "lib/ipi.h"
#include "lib/pcpu.h"
#include "lib/string.h"
#include "mem/mappings.h"
#include "mem/phys.h"
#include "mem/virt.h"
#include "time/tsc.h"

/**
 * The id of the current cpu
 */
CPU_LOCAL uintptr_t g_syscall_stack;

typedef struct syscall_frame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    union {
        uint64_t arg4;
        uint64_t r10;
    };
    union {
        uint64_t arg6;
        uint64_t r9;
    };
    union {
        uint64_t arg5;
        uint64_t r8;
    };
    uint64_t rbp;
    union {
        uint64_t arg1;
        uint64_t rdi;
    };
    union {
        uint64_t arg2;
        uint64_t rsi;
    };
    union {
        uint64_t arg3;
        uint64_t result2;
        uint64_t rdx;
    };
    uint64_t rcx;
    uint64_t rbx;
    union {
        uint64_t syscall;
        uint64_t result;
        uint64_t rax;
    };
} syscall_frame_t;

/**
 * are we done with early memory
 */
LATE_RO static bool m_early_done = false;

/**
 * Is using monitor supported
 */
LATE_RO bool g_monitor_supported = false;

static void copy_from_user(void* dst, uintptr_t src, size_t size) {
    asm("stac");
    ASSERT(src <= (uintptr_t)vmar_end(&g_user_memory));
    memcpy(dst, (void*)src, size);
    asm("clac");
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

static void user_access_enable(void) {
    asm("stac");
}

static void user_access_disable(void) {
    asm("clac");
}

__attribute__((target("sse3")))
static void monitor_wait(_Atomic(uint32_t)* addr, uint32_t expected) {
    // ensure that we even support using the monitor instruction
    ASSERT(g_monitor_supported);

    for (;;) {
        user_access_enable();
        uint32_t value = atomic_load_explicit(addr, memory_order_acquire);
        user_access_disable();

        if (value != expected) {
            return;
        }

        _mm_monitor(addr, 0, 0);

        user_access_enable();
        value = atomic_load_explicit(addr, memory_order_acquire);
        user_access_disable();
        if (value != expected) {
            return;
        }

        // BIT1 == break on interrupt even with IF=0
        _mm_mwait(BIT1, 0);
    }
}

OMIT_ENDBR void syscall_handler(syscall_frame_t* frame) {
    err_t err = NO_ERROR;
    ipi_enable();

    switch (frame->syscall) {
        case SYSCALL_DEBUG_PRINT: {
            char buffer[512];
            int len = MIN(frame->arg2, sizeof(buffer) - 1);
            copy_from_user(buffer, frame->arg1, len);
            buffer[len] = '\0';
            debug_print("%.*s", len, buffer);
        } break;

        case SYSCALL_HEAP_ALLOC: {
            vmar_lock();
            vmar_t* region = vmar_allocate(&g_user_memory, frame->arg1, nullptr);
            if (region != nullptr) {
                vmar_set_name(region, "heap");
            }
            vmar_unlock();

            if (region == NULL) {
                frame->result = 0;
            } else {
                frame->result = (uintptr_t)region->base;
            }
        } break;

        case SYSCALL_STACK_ALLOC: {
            stack_alloc_t alloc = {};

            const char* name = (const char*)frame->arg2;
            CHECK((void*)name <= vmar_end(&g_user_memory));

            if (IS_ERROR(user_stack_alloc(&alloc, name, frame->arg1))) {
                frame->result = 0;
                frame->result2 = 0;
            } else {
                frame->result = (uintptr_t)alloc.stack;
                frame->result2 = (uintptr_t)alloc.shadow_stack;
            }
        } break;

        case SYSCALL_TIMER_SET_DEADLINE: {
            tsc_timer_set_deadline(frame->arg1);
        } break;

        case SYSCALL_TIMER_CLEAR: {
            // TODO: lapic timer
            tsc_timer_clear();
        } break;

        case SYSCALL_INTERRUPT_ACK: {
            lapic_eoi();
        } break;

        case SYSCALL_MONITOR_WAIT: {
            monitor_wait((_Atomic(uint32_t)*)frame->arg1, frame->arg2);
        } break;

        case SYSCALL_EARLY_INTERRUPT_SET_HANDLER: {
            CHECK(!m_early_done);
            intr_set_user_handler(frame->arg1, (interrupt_handler_t)frame->arg2);
        } break;

        case SYSCALL_EARLY_SET_THREAD_ENTRY_THUNK: {
            CHECK(!m_early_done);
            CHECK(g_shadow_stack_thread_entry_thunk == 0);
            CHECK((uintptr_t)g_runtime_region.base <= frame->arg1);
            CHECK(frame->arg1 < (uintptr_t)vmar_end(&g_runtime_region));
            g_shadow_stack_thread_entry_thunk = frame->arg1;
        } break;

        case SYSCALL_EARLY_DONE: {
            // perform last cleanups and reclaim all init code
            RETHROW(handle_early_done());
            reclaim_init_mem();
        } break;

        default:
            ASSERT(false, "syscall: Unknown syscall: %ld", frame->syscall);
    }

cleanup:
    ipi_disable();
    ASSERT(!IS_ERROR(err), "syscall: error while performing syscall");
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
    __wrmsr(MSR_IA32_CSTAR, flags.packed);
}
