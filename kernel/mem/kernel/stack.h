#pragma once

#include "lib/except.h"

/**
 * Allocates a stack of the given size, returning the pointer
 * to the start (highest pointer) in the stack
 */
err_t stack_alloc(const char* name, size_t stack_size, void** stack);
