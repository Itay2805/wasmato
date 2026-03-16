#pragma once

#include "lib/except.h"

INIT_CODE err_t load_runtime(void);

INIT_CODE void runtime_free_early_stacks(void);

INIT_CODE void runtime_start(void);
