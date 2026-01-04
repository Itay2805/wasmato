#include "thread.h"

#include <arch/gdt.h>
#include <arch/intrin.h>
#include <lib/list.h>
#include <lib/printf.h>
#include <lib/string.h>
#include <sync/spinlock.h>
#include <mem/alloc.h>

#include "scheduler.h"
#include "mem/stack.h"
#include "time/tsc.h"

/**
 * The size of the extended state
 */
size_t g_extended_state_size = 0;

static thread_t* thread_alloc() {
    thread_t* thread = NULL;

    thread = mem_alloc(sizeof(thread_t) + g_extended_state_size, alignof(thread_t));
    if (thread == NULL) {
        return NULL;
    }

    // switch to a dead state, just so we can wake it up properly
    thread_switch_status(thread, THREAD_STATUS_IDLE, THREAD_STATUS_DEAD);

    // allocate the stack
    if (IS_ERROR(stack_alloc(&thread->stack_start, &thread->stack_end))) {
        mem_free(thread, sizeof(thread_t) + g_extended_state_size, alignof(thread_t));
        return NULL;
    }

    return thread;
}

void thread_switch_status(thread_t* thread, thread_status_t old_value, thread_status_t new_value) {
    thread_status_t current = old_value;
    for (int i = 0; !atomic_compare_exchange_strong(&thread->status, &current, new_value); current = old_value, i++) {
        ASSERT(!(old_value == THREAD_STATUS_WAITING && current == THREAD_STATUS_RUNNABLE), "waiting for WAITING but is RUNNABLE");

        // TODO: the go code this is inspired by has some yield mechanism, can we use it? do we want to?
    }
}

thread_t* thread_create(thread_entry_t callback, void* arg, const char* name_fmt, ...) {
    thread_t* thread = thread_alloc();
    if (thread == NULL) {
        return NULL;
    }
    ASSERT(thread->status == THREAD_STATUS_DEAD);

    // set the name
    va_list va;
    va_start(va, name_fmt);
    snprintf_(thread->name, sizeof(thread->name) - 1, name_fmt, va);
    va_end(va);

    // set the thread callback as the function to jump to and the rdi
    // as the first parameter, we are going to push to the stack the
    // thread_exit function to ensure it will exit the thread at the end,
    // this also ensures the stack is properly aligned at its entry
    uintptr_t* stack = thread->stack_start - 16;
    *--stack = (uintptr_t)thread_exit;
    thread->cpu_state = (void*)stack - sizeof(*thread->cpu_state);
    thread->cpu_state->rip = (uintptr_t)callback;
    thread->cpu_state->rdi = (uintptr_t)arg;

    // setup the extended state
    xsave_legacy_region_t* extended_state = (xsave_legacy_region_t*)thread->extended_state;
    extended_state->mxscr = 0x00001f80;

    // we are going to start it in a parked state, and the caller needs
    // to actually queue it
    thread_switch_status(thread, THREAD_STATUS_DEAD, THREAD_STATUS_WAITING);

    return thread;
}

/**
 * Finalizes the switch to the thread, including
 * actually jumping to it
 */
noreturn void thread_resume_finish(thread_frame_t* frame);

void thread_resume(thread_t* thread) {
    // Restore the extended state
    // TODO: support for xrstors when available
    __builtin_ia32_xrstor64(thread->extended_state, ~0ull);

    // and now we can jump to the thread
    thread_resume_finish(thread->cpu_state);
}

void thread_save_extended_state(thread_t* thread) {
    // Save the extended state
    // TODO: support for using xsaves which has both init and modified and compact
    //       optimizations, we won't support xsavec since it does not have the modified
    //       optimization
    __builtin_ia32_xsaveopt64(thread->extended_state, ~0ull);
}

void thread_free(thread_t* thread) {
    mem_free(thread, sizeof(thread_t) + g_extended_state_size, alignof(thread_t));
}

void thread_exit() {
    scheduler_exit();
}
