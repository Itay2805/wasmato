#include "thread.h"

#include "arch/intrin.h"
#include "lib/assert.h"

void do_thread_switch(thread_t* to, thread_t* from);
noreturn void do_thread_resume(thread_t* to);

static void save_thread_context(thread_t* thread) {
    // TODO: xsave
}

static void restore_thread_context(thread_t* thread) {
    // restore the tcb
    _writefsbase_u64((uintptr_t)thread->tcb);

    // TODO: xrstr
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
