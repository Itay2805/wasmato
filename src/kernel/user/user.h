#pragma once

#include <stdnoreturn.h>

void usermode_jump(void* arg, void* rip, void* rsp);
void usermode_jump_shstk(void* arg, void* rip, void* rsp, void* ssp_token);
