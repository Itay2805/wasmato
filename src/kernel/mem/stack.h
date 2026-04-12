#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "lib/except.h"

/**
 * Are shadow stacks enabled
 */
extern bool g_shadow_stack_supported;

typedef struct stack_alloc {
    void* stack;
    void* shadow_stack;
} stack_alloc_t;

err_t stack_alloc(stack_alloc_t* alloc, const char* name, size_t size, bool user);

void stack_free(void* ptr, bool user);
