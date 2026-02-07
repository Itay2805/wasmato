#pragma once

#include <stdint.h>

void init_syscall(void);

uintptr_t switch_syscall_stack(uintptr_t value);
