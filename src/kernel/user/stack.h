#pragma once

#include <stddef.h>

#include "lib/except.h"

/**
 * Are user shadow stacks enabled
 */
extern bool g_shadow_stack_supported;

typedef struct stack_alloc {
    void* stack;
    void* shadow_stack;
} stack_alloc_t;

err_t user_stack_alloc(stack_alloc_t* alloc, void* name, size_t size);

void user_stack_free(void* ptr);
