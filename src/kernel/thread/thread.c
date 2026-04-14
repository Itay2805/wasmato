#include "thread.h"

#include <x86intrin.h>

#include "sched.h"
#include "arch/intrin.h"
#include "lib/assert.h"

#include "arch/cpuid.h"
#include "arch/gdt.h"
#include "arch/regs.h"
#include "lib/atomic.h"
#include "lib/except.h"
#include "lib/printf.h"
#include "lib/tsc.h"
#include "mem/alloc.h"
#include "mem/stack.h"

/**
 * The saved state when switching between threads
 */
typedef struct thread_entry_frame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rip;
    uint64_t rsi;
    uint64_t rdx;
} PACKED thread_entry_frame_t;

/**
 * The allocator used to allocate threads
 */
static mem_alloc_t m_thread_alloc;

INIT_CODE void init_threads(void) {
    // Get the extended state size to allocate along size the thread itself
    uint32_t a, xsave_area_size, c, d;
    __cpuid_count(CPUID_EXTENDED_STATE, CPUID_EXTENDED_STATE_MAIN_LEAF, a, xsave_area_size, c, d);
    mem_alloc_init(&m_thread_alloc, sizeof(thread_t) + xsave_area_size, alignof(thread_t));
}

static void thread_free(thread_t* thread) {
    if (thread != nullptr) {
        if (thread->kernel_stack != nullptr) {
            stack_free(thread->kernel_stack, false);
        }

        if (thread->user_stack != nullptr) {
            stack_free(thread->user_stack, true);
        }

        mem_free(&m_thread_alloc, thread);
    }
}

thread_t* thread_create(thread_entry_t entry_point, void* arg, thread_flags_t flags, const char* name_fmt, ...) {
    err_t err = NO_ERROR;

    // allocate the thread itself
    thread_t* thread = mem_alloc(&m_thread_alloc);
    CHECK_ERROR(thread != nullptr, ERROR_OUT_OF_MEMORY);
    memset(thread, 0, m_thread_alloc.object_size);
    thread->flags = flags;

    // start with ref count of one
    thread->ref_count = 1;
    atomic_store_relaxed(&thread->state, THREAD_STATE_IDLE);

    // just used for debug
    va_list args = {};
    va_start(args, name_fmt);
    int count = vsnprintf(thread->name, sizeof(thread->name), name_fmt, args);
    va_end(args);
    thread->name[count] = '\0';

    // allocate the stacks
    stack_alloc_t kernel_stack;
    RETHROW(stack_alloc(&kernel_stack, thread->name, SIZE_4KB, false));
    thread->kernel_stack = kernel_stack.stack;
    thread->kernel_ssp = kernel_stack.shadow_stack;

    // allocate the stacks
    if (flags & THREAD_FLAG_USER) {
        stack_alloc_t user_stack;
        RETHROW(stack_alloc(&user_stack, thread->name, SIZE_32KB, true));
        thread->user_stack = user_stack.stack;
        thread->user_ssp = user_stack.shadow_stack;
    }

    // setup the rsp
    void* rsp = thread->kernel_stack - 8;
    rsp -= sizeof(thread_entry_frame_t);
    thread_entry_frame_t* entry_frame = rsp;
    thread->kernel_rsp = rsp;

    // setup the entry frame
    entry_frame->rip = (uintptr_t)thread_entry_thunk;
    entry_frame->rsi = (uintptr_t)entry_point;
    entry_frame->rdx = (uintptr_t)arg;

    // setup the extended state
    xsave_legacy_region_t* extended_state = (xsave_legacy_region_t*)thread->extended_state;
    extended_state->mxscr = 0x00001f80;

cleanup:
    if (IS_ERROR(err)) {
        thread_free(thread);
        thread = nullptr;
    }
    return thread;
}

void thread_start(thread_t* thread) {
    bool irq_state = irq_save();
    ASSERT(atomic_load_relaxed(&thread->state) == THREAD_STATE_IDLE);
    atomic_store_relaxed(&thread->state, THREAD_STATE_READY);
    scheduler_enqueue(thread);
    irq_restore(irq_state);
}

thread_t* thread_get(thread_t* thread) {
    thread->ref_count++;
    return thread;
}

void thread_put(thread_t* thread) {
    if (--thread->ref_count == 0) {
        ASSERT(atomic_load_relaxed(&thread->state) == THREAD_STATE_DEAD);
        // TODO: place a thread in a queue for being deleted
        //       and wakeup the thread GC
    }
}

void thread_exit(void) {
    thread_t* current = get_current_thread();
    atomic_store_relaxed(&current->state, THREAD_STATE_DEAD);
    irq_disable();
    scheduler_schedule();
}

void thread_sleep(size_t ms) {
    thread_t* thread = get_current_thread();
    bool irq_state = irq_save();
    atomic_store_relaxed(&thread->state, THREAD_STATE_PARKING);
    scheduler_schedule_deadline(tsc_ms_deadline(ms));
    irq_restore(irq_state);
}

void do_thread_switch(thread_t* to, thread_t* from);
noreturn void do_thread_resume(thread_t* to);

static void save_thread_context(thread_t* thread) {
    // save the shadow stack
    if (g_shadow_stack_supported) {
        thread->user_ssp = (void*)__rdmsr(MSR_IA32_PL3_SSP);
    }

    // Save modified extended state
    _xsaveopt64(thread->extended_state, ~0U);
}

static void restore_thread_context(thread_t* thread) {
    // restore the shadow stack
    if (g_shadow_stack_supported) {
        __wrmsr(MSR_IA32_PL3_SSP, (uintptr_t)thread->user_ssp);
    }

    // set the stack for the next thread
    tss_set_rsp0(thread->kernel_stack);

    // Restore extended state
    _xrstor64(thread->extended_state, ~0U);
}

void thread_switch(thread_t* to, thread_t* from) {
    save_thread_context(from);
    restore_thread_context(to);
    do_thread_switch(to, from);
}

void thread_resume(thread_t* thread) {
    restore_thread_context(thread);
    do_thread_resume(thread);
}
