#pragma once

#include "arch/regs.h"
#include "lib/except.h"
#include "lib/list.h"
#include "mem/region.h"

typedef void (*thread_entry_t)(void *arg);

/**
 * The saved state when switching between threads
 */
typedef struct thread_frame {
    rflags_t rflags;
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rip;
} PACKED thread_frame_t;

typedef struct thread {
    // The CPU state of the thread, must be first since its
    // accessed from assembly
    thread_frame_t* cpu_state;

    // The thread name, not null terminated
    char name[256];

    // either a freelist link or the scheduler link
    list_t link;

    // Stacks used for running interrupts, to ensure that
    // we can properly reschedule from interrupts
    void* kernel_stack;

    // the stack regions
    region_t* stack_region;

    // the entry and argument to pass to the entry
    thread_entry_t entry;
    void* arg;

    // The node for the scheduler
    list_entry_t scheduler_node;

    // The extended state of the thread, must be aligned
    // for XSAVE to work
    __attribute__((aligned(64)))
    uint8_t extended_state[];
} thread_t;

/**
 * The size of the extended state
 */
extern size_t g_extended_state_size;

/**
* Create a new kernel thread, you need to schedule it yourself
*/
err_t thread_create(thread_t** out_thread, thread_entry_t callback, void* arg, const char* name_fmt, ...);

/**
* Create a new kernel thread, you need to schedule it yourself
*/
err_t user_thread_create(thread_t** out_thread, void* callback, void* arg, const char* name_fmt, ...);

/**
 * Reset the kernel thread to its initial state
 */
void thread_reset(thread_t* thread);

/**
 * Resume a thread, destroying the
 * current context that we have
 */
void thread_switch(thread_t* from, thread_t* to);

/**
 * Jump to a thread, destroying the current context
 */
void thread_jump(thread_t* to);

/**
 * Free the thread completely
 */
void thread_free(thread_t* thread);
