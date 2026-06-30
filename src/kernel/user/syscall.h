#pragma once

#include <stdint.h>

#include "lib/pcpu.h"

void assert_user_range(const void* addr, size_t size);

void init_syscall(void);
