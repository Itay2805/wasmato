#pragma once

#include <stdint.h>
#include "buffer.h"

#define WASM_PAGE_SIZE (65536)

typedef enum wasm_section_id : uint8_t {
    WASM_SECTION_CUSTOM = 0,
    WASM_SECTION_TYPE = 1,
    WASM_SECTION_IMPORT = 2,
    WASM_SECTION_FUNCTION = 3,
    WASM_SECTION_TABLE = 4,
    WASM_SECTION_MEMORY = 5,
    WASM_SECTION_GLOBAL = 6,
    WASM_SECTION_EXPORT = 7,
    WASM_SECTION_START = 8,
    WASM_SECTION_ELEMENT = 9,
    WASM_SECTION_CODE = 10,
    WASM_SECTION_DATA = 11,
    WASM_SECTION_DATA_COUNT = 12,
    WASM_SECTION_TAG = 13,
} wasm_section_id_t;

typedef struct wasm_section {
    wasm_section_id_t id;
    buffer_t contents;
} wasm_section_t;
