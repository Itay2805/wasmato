#include "wasi.h"
#include "lib/defs.h"
#include "wasm/wasm.h"
#include "lib/cpp_magic.h"
#include "lib/string.h"

void* wasi_resolve_import(const char* name, wasm_type_t* type) {
    return nullptr;
}
