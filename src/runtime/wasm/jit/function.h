#pragma once

#include <stdint.h>

#include "jit_internal.h"
#include "lib/except.h"

err_t jit_prepare_function(jit_context_t* ctx, uint32_t funcidx);

err_t jit_function(jit_context_t* context, uint32_t funcidx);
