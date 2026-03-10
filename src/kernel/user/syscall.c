#include "syscall.h"
#include "uapi/syscall.h"

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
    uint64_t arg4;
    uint64_t arg6;
    uint64_t arg5;
    uint64_t rbp;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint64_t rcx;
    uint64_t rbx;
    union {
        uint64_t syscall;
        uint64_t result;
    };
} syscall_frame_t;

/**
 * are we done with early memory
 */
LATE_RO static bool m_early_done = false;

static void copy_from_user(void* dst, uintptr_t src, size_t size) {
    asm("stac");
    memcpy(dst, (void*)src, size);
    asm("clac");
}

INIT_CODE static err_t handle_early_done(void) {
    err_t err = NO_ERROR;

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
            vmar_unlock();

            if (region == NULL) {
                frame->result = 0;
            } else {
                frame->result = (uintptr_t)region->base;
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

        case SYSCALL_EARLY_INTERRUPT_SET_HANDLER: {
            CHECK(!m_early_done);
            intr_set_user_handler(frame->arg1, (interrupt_handler_t)frame->arg2);
        } break;

        case SYSCALL_EARLY_DONE: {
            // perform last cleanups and reclaim all init code
            RETHROW(handle_early_done());
            reclaim_init_mem();

            vmar_dump(&g_kernel_memory);
            vmar_dump(&g_user_memory);
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
