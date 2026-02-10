#include "syscall.h"
#include "uapi/syscall.h"

#include "arch/gdt.h"
#include "arch/intrin.h"
#include "arch/regs.h"
#include "pcpu.h"
#include "lib/string.h"
#include "mem/mappings.h"

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
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
} syscall_frame_t;

static void copy_from_user(void* dst, uintptr_t src, size_t size) {
    asm("stac");
    memcpy(dst, (void*)src, size);
    asm("clac");
}

void syscall_handler(syscall_frame_t* frame) {
    switch (frame->rax) {
        case SYSCALL_DEBUG_PRINT: {
            char buffer[512];
            copy_from_user(buffer, frame->rdi, frame->rsi);
            debug_print("%.*s", (int)frame->rsi, buffer);
        } break;

        case SYSCALL_HEAP_ALLOC: {
            vmar_lock();
            vmar_t* region = vmar_allocate(&g_user_memory, frame->rdi, nullptr);
            vmar_unlock();

            if (region == NULL) {
                frame->rax = 0;
            } else {
                frame->rax = (uintptr_t)region->base;
            }
        } break;

        default:
            ASSERT(!"Unknown syscall");
    }
}

void syscall_entry(void);

void init_syscall() {
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

uintptr_t switch_syscall_stack(uintptr_t value) {
    uintptr_t old = g_syscall_stack;
    g_syscall_stack = value;
    return old;
}