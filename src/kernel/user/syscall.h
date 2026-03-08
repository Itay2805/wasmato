#pragma once

#include <stdint.h>

#include "lib/pcpu.h"

/**
 * The stack used for syscalls, is also used to save the
 * user rsp while we do stuff
 */
extern CPU_LOCAL uintptr_t g_syscall_stack;

void init_syscall(void);
