#pragma once

#include "wasm/wasm.h"

void* wasi_resolve_import(const char* name, wasm_type_t* type);
