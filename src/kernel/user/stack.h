#pragma once

#include <stddef.h>

#include "lib/except.h"

/**
 * Are user shadow stacks enabled
 */
extern bool g_shadow_stack_supported;

/**
 * The entry point of a thread, which is pushed
 * into the shadow stack so it can be returned to
 */
extern uintptr_t g_shadow_stack_thread_entry_thunk;

typedef struct stack_alloc {
    void* stack;
    void* shadow_stack;
} stack_alloc_t;

err_t user_stack_alloc(stack_alloc_t* alloc, const char* name, size_t size);

void user_stack_free(void* ptr);
