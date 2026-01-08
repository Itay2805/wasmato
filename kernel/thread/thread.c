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

static void thread_exit(void) {
    scheduler_exit();
}

static void thread_entry(void) {
    thread_t* thread = scheduler_get_current_thread();
    thread->entry(thread->arg);
}

err_t thread_create(thread_t** out_thread, thread_entry_t callback, void* arg, const char* name_fmt, ...) {
    err_t err = NO_ERROR;

    // allocate and zero the thread struct
    size_t thread_total_size = sizeof(thread_t) + g_extended_state_size;
    thread_t* thread = mem_alloc(thread_total_size, alignof(thread_t));
    CHECK_ERROR(thread != NULL, ERROR_OUT_OF_MEMORY);
    memset(thread, 0, thread_total_size);

    // allocate the stack
    RETHROW(stack_alloc(SIZE_32KB, &thread->stack_start, &thread->stack_end));

    // set the name
    va_list va;
    va_start(va, name_fmt);
    snprintf_(thread->name, sizeof(thread->name) - 1, name_fmt, va);
    va_end(va);

    // remember the entry
    thread->entry = callback;
    thread->arg = arg;

    // set the thread callback as the function to jump to and the rdi
    // as the first parameter, we are going to push to the stack the
    // thread_exit function to ensure it will exit the thread at the end,
    // this also ensures the stack is properly aligned at its entry
    thread_reset(thread);

    // setup the extended state
    xsave_legacy_region_t* extended_state = (xsave_legacy_region_t*)thread->extended_state;
    extended_state->mxscr = 0x00001f80;

    *out_thread = thread;

cleanup:
    if (IS_ERROR(err)) {
        if (thread != NULL) {
            thread_free(thread);
        }
    }

    return err;
}

void thread_reset(thread_t* thread) {
    uintptr_t* stack = thread->stack_start - 16;
    *--stack = (uintptr_t)thread_exit;
    thread->cpu_state = (void*)stack - sizeof(*thread->cpu_state);
    thread->cpu_state->rflags = (rflags_t){
        .always_one = 1,
        .IF = 1, // we want interrupts
    };
    thread->cpu_state->rbp = 0;
    thread->cpu_state->rip = (uintptr_t)thread_entry;
}

/**
 * Finalizes the switch to the thread, including
 * actually jumping to it
 */
void thread_do_switch(thread_t* from, thread_t* to);
void thread_do_jump(thread_t* to);

void thread_switch(thread_t* from, thread_t* to) {
    // Save the extended state of current thread
    // TODO: support for using xsaves which has both init and modified and compact
    //       optimizations, we won't support xsavec since it does not have the modified
    //       optimization
    __builtin_ia32_xsaveopt64(from->extended_state, ~0ull);

    // Restore the extended state
    // TODO: support for xrstors when available
    __builtin_ia32_xrstor64(to->extended_state, ~0ull);

    // and now we can jump to the thread
    thread_do_switch(from, to);
}

void thread_jump(thread_t* to) {
    // Restore the extended state
    // TODO: support for xrstors when available
    __builtin_ia32_xrstor64(to->extended_state, ~0ull);

    // and now we can jump to the thread
    thread_do_jump(to);
}

void thread_free(thread_t* thread) {
    ASSERT(thread != NULL);
    // TODO: free stacks
    mem_free(thread, sizeof(thread_t) + g_extended_state_size, alignof(thread_t));
}
