#pragma once

#include "proc/proc.h"
#include "wasm/wasm.h"
#include <stdint.h>

extern uint64_t g_acpi_rsdp;

void* wasmato_resolve_import(const char* name, wasm_proc_t* proc, wasm_type_t* type);
