#pragma once
#include "lib/except.h"

/**
 * Initialize early allocator
 */
err_t init_early_mem(void);

/**
 * Get the top memory pointer from the early allocator
 */
void* early_alloc_get_top(void);
