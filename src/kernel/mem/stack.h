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

typedef enum stack_alloc_flag {
    /**
     * Allocate a user stack
     */
    STACK_ALLOC_USER = BIT0,

    /**
     * Allocate all the stack pages right away
     * so no faults will happen on the access
     * to it
     */
    STACK_ALLOC_FILL = BIT1,
} stack_alloc_flag_t;

err_t stack_alloc(stack_alloc_t* alloc, const char* name, size_t size, stack_alloc_flag_t flags);

void stack_free(void* ptr, bool user);
