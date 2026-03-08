#pragma once
#include "lib/except.h"

/**
 * Initialize early allocator
 */
INIT_CODE err_t init_early_mem(void);

/**
 * Get the top memory pointer from the early allocator
 */
INIT_CODE void* early_alloc_get_top(void);
