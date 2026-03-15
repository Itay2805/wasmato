#include "thread.h"

#include <x86intrin.h>
#include "arch/intrin.h"
#include "lib/assert.h"

void do_thread_switch(thread_t* to, thread_t* from);
noreturn void do_thread_resume(thread_t* to);

static void save_thread_context(thread_t* thread) {
    // Save modified extended state
    _xsaveopt64(thread->extended_state, ~0U);
}

static void restore_thread_context(thread_t* thread) {
    // Restore FS base (thread-local storage pointer).
    _writefsbase_u64((uintptr_t)thread->tcb);

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
