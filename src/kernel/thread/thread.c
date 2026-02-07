#include "thread.h"

#include "scheduler.h"
#include "arch/gdt.h"
#include "arch/paging.h"
#include "lib/printf.h"
#include "lib/string.h"
#include "mem/mappings.h"
#include "mem/region.h"
#include "mem/internal/phys.h"
#include "mem/kernel/alloc.h"

/**
 * The size of the extended state
 */
size_t g_extended_state_size = 0;

//----------------------------------------------------------------------------------------------------------------------
// Kernel-mode thread
//----------------------------------------------------------------------------------------------------------------------

static void thread_exit(void) {
    scheduler_exit();
}

static void thread_entry(void) {
    thread_t *thread = scheduler_get_current_thread();
    thread->entry(thread->arg);
}

void thread_reset(thread_t* thread) {
    ASSERT(thread->stack_region == NULL);
    uintptr_t* stack = thread->kernel_stack - 16;
    *--stack = (uintptr_t)thread_exit;
    thread->cpu_state = (void*)stack - sizeof(*thread->cpu_state);
    thread->cpu_state->rflags = (rflags_t){
        .always_one = 1,
        .IF = 1, // we want interrupts
    };
    thread->cpu_state->rbp = 0;
    thread->cpu_state->rip = (uintptr_t)thread_entry;
}

err_t thread_create(thread_t** out_thread, thread_entry_t callback, void* arg, const char* name_fmt, ...) {
    err_t err = NO_ERROR;

    // allocate and zero the thread struct
    size_t thread_total_size = sizeof(thread_t) + g_extended_state_size;
    thread_t* thread = phys_alloc(thread_total_size);
    CHECK_ERROR(thread != NULL, ERROR_OUT_OF_MEMORY);
    memset(thread, 0, thread_total_size);

    // set the name
    va_list va;
    va_start(va, name_fmt);
    vsnprintf_(thread->name, sizeof(thread->name) - 1, name_fmt, va);
    va_end(va);

    // allocate the stack for the kernel
    void* kernel_stack = phys_alloc(PAGE_SIZE);
    CHECK(kernel_stack != NULL);
    thread->kernel_stack = kernel_stack + PAGE_SIZE;

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

//----------------------------------------------------------------------------------------------------------------------
// User-mode thread
//----------------------------------------------------------------------------------------------------------------------

void thread_do_jump_to_user(void* rip, void* stack, void* arg);

static void thread_user_entry(void) {
    thread_t* thread = scheduler_get_current_thread();
    thread_do_jump_to_user(thread->entry, region_end(thread->stack_region) - PAGE_SIZE, thread->arg);
}

err_t user_thread_create(thread_t** out_thread, void* callback, void* arg, const char* name_fmt, ...) {
    err_t err = NO_ERROR;

    // allocate and zero the thread struct
    size_t thread_total_size = sizeof(thread_t) + g_extended_state_size;
    thread_t* thread = phys_alloc(thread_total_size);
    CHECK_ERROR(thread != NULL, ERROR_OUT_OF_MEMORY);
    memset(thread, 0, thread_total_size);

    // set the name
    va_list va;
    va_start(va, name_fmt);
    vsnprintf_(thread->name, sizeof(thread->name) - 1, name_fmt, va);
    va_end(va);

    // allocate the user stack
    thread->stack_region = region_allocate_user_stack(SIZE_32KB);
    CHECK_ERROR(thread->stack_region != NULL, ERROR_OUT_OF_MEMORY);

    // allocate the stack for the kernel
    void* kernel_stack = phys_alloc(PAGE_SIZE);
    CHECK(kernel_stack != NULL);
    thread->kernel_stack = kernel_stack + PAGE_SIZE;

    // set the entry point as something that will jump into the usermode code
    uintptr_t* stack = thread->kernel_stack - 16;
    thread->cpu_state = (void*)stack - sizeof(*thread->cpu_state);
    thread->cpu_state->rflags = (rflags_t){
        .always_one = 1,
        .IF = 1, // we want interrupts
    };
    thread->cpu_state->rbp = 0;
    thread->cpu_state->rip = (uintptr_t)thread_user_entry;

    // remember the arg and entry code
    thread->entry = callback;
    thread->arg = arg;

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

//----------------------------------------------------------------------------------------------------------------------
// Thread scheduling utils
//----------------------------------------------------------------------------------------------------------------------

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

    // set the kernel stack
    tss_set_irq_stack(to->kernel_stack - 16);

    // and now we can jump to the thread
    thread_do_switch(from, to);
}

void thread_jump(thread_t* to) {
    // Restore the extended state
    // TODO: support for xrstors when available
    __builtin_ia32_xrstor64(to->extended_state, ~0ull);

    // set the kernel stack
    tss_set_irq_stack(to->kernel_stack - 16);

    // and now we can jump to the thread
    thread_do_jump(to);
}

void thread_free(thread_t* thread) {
    ASSERT(thread != NULL);

    if (thread->stack_region != NULL) {
        region_free(thread->stack_region);
    }

    phys_free(thread->kernel_stack - PAGE_SIZE, PAGE_SIZE);
    phys_free(thread, sizeof(thread_t) + g_extended_state_size);
}
