#pragma once

#include <stddef.h>

void* user_stack_alloc(void* name, size_t size);

void user_stack_free(void* ptr);
