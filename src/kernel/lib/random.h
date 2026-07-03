#pragma once

#include "lib/defs.h"
#include <stddef.h>

/**
 * Generate random from the arch itself, useful for early boot stuff
 */
INIT_CODE void boot_random_fill(void* data, size_t size);
