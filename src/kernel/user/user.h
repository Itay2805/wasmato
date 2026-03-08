#pragma once

#include <stdnoreturn.h>

INIT_CODE noreturn void usermode_jump(void* arg, void* rip, void* rsp);
