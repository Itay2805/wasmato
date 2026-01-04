#pragma once

#include "lib/except.h"

/**
 * Allocate a stack that has a gurad page
 *
 * The stack goes from the start to the end, meaning that
 * start is higher than end address wise
 */
err_t stack_alloc(void** stack_start, void** stack_end);
