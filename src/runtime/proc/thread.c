#include "thread.h"
#include "core/thread.h"

#include "alloc/alloc.h"
#include "arch/cpuid.h"
#include "lib/except.h"
#include "lib/printf.h"
#include "uapi/syscall.h"

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
 * The size of a single thread with its extended
 * save state
 */
static size_t m_thread_size = 0;

/**
 * The size of the TLS, not including the TCB
 */
static size_t m_thread_tls_size = 0;

void init_threads(size_t tls_size) {
    m_thread_tls_size = ALIGN_UP(tls_size, 16);
    m_thread_size = sizeof(thread_t);

    // Get the extended state size to allocate along size the thread itself
    uint32_t a, xsave_area_size, c, d;
    __cpuid_count(CPUID_EXTENDED_STATE, CPUID_EXTENDED_STATE_MAIN_LEAF, a, xsave_area_size, c, d);
    m_thread_size += xsave_area_size;
}

static void thread_free(thread_t* thread) {
    if (thread != nullptr) {
        if (thread->stack != nullptr) {
            sys_stack_free(thread->stack);
        }
        if (thread->tcb != nullptr) {
            mem_free((void*)thread->tcb - m_thread_tls_size);
        }
        if (thread->name != nullptr) {
            mem_free(thread->name);
        }
        mem_free(thread);
    }
}

thread_t* thread_vcreate(thread_entry_t entry_point, void* arg, const char* name_fmt, va_list args) {
    err_t err = NO_ERROR;

    // allocate the thread itself
    thread_t* thread = mem_alloc_aligned(m_thread_size, 64);
    CHECK_ERROR(thread != nullptr, ERROR_OUT_OF_MEMORY);
    memset(thread, 0, m_thread_size);

    // start with ref count of one
    thread->ref_count = 1;
    thread->state = THREAD_STATE_PARKED;
    thread->lock = IRQ_SPINLOCK_INIT;

    // TODO: something simpler? better? just get a pointer and
    //       copy from the user?
    int count = vsnprintf_(nullptr, 0, name_fmt, args);
    thread->name = mem_alloc(count + 1);
    CHECK(thread->name != nullptr);
    vsnprintf_(thread->name, count + 1, name_fmt, args);
    thread->name[count] = '\0';

    // allocate the stacks
    sys_stack_alloc_t stack = sys_stack_alloc(SIZE_32KB, thread->name);
    CHECK_ERROR(stack.stack != nullptr, ERROR_OUT_OF_MEMORY);
    thread->stack = stack.stack;
    thread->ssp = stack.shadow_stack;

    // allocate the tcb and set it up
    size_t tls_size = m_thread_tls_size + sizeof(tcb_t);
    void* tls = mem_alloc_aligned(tls_size, 16);
    CHECK_ERROR(tls != nullptr, ERROR_OUT_OF_MEMORY);
    memset(tls, 0, tls_size);
    thread->tcb = tls + m_thread_tls_size;
    thread->tcb->tcb = thread->tcb;

    // setup the rsp
    void* rsp = thread->stack - 16;
    rsp -= sizeof(thread_entry_frame_t);
    thread_entry_frame_t* entry_frame = rsp;
    thread->rsp = rsp;

    // setup the entry frame
    entry_frame->rip = (uintptr_t)thread_entry_thunk;
    entry_frame->rsi = (uintptr_t)entry_point;
    entry_frame->rdx = (uintptr_t)arg;

cleanup:
    if (IS_ERROR(err)) {
        thread_free(thread);
        thread = nullptr;
    }
    return thread;
}

void thread_get(thread_t* thread) {
    thread->ref_count++;
}

void thread_put(thread_t* thread) {
    if (--thread->ref_count == 0) {
        ASSERT(thread->state == THREAD_STATE_DEAD);
        // TODO: place a thread in a queue for being deleted
        //       and wakeup the thread GC
    }
}
