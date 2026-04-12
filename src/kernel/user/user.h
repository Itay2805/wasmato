#pragma once

#include <stdnoreturn.h>

noreturn void usermode_jump(void* arg, void* rip, void* rsp);
