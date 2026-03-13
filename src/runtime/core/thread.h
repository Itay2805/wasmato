#pragma once

#include <stdnoreturn.h>

#include "proc/thread.h"

/**
 * Switching to another thread, saving
 * the current context first
 */
void thread_switch(thread_t* to, thread_t* from);

/**
 * Resumes a thread, ignoring the current context
 */
noreturn void thread_resume(thread_t* thread);

/**
 * The thunk that we should jump to
 * when starting a thread
 */
void thread_entry_thunk(void);
