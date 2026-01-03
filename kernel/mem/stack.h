#pragma once

#include "lib/except.h"

/**
 * Allocate a stack that has a gurad page
 */
err_t stack_alloc(void** stack);
