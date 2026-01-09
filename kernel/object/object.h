#pragma once

#include <stdarg.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "lib/defs.h"

typedef enum object_flag : size_t {
    /**
     * This is a static object, meaning
     * it should never be freed
     */
    OBJECT_STATIC = BIT0,
} object_flag_t;

typedef enum object_type : uint8_t {
    /**
     * Virtual Memory Address Region
     */
    OBJECT_TYPE_VMAR,

    /**
     * Virtual Memory Object
     */
    OBJECT_TYPE_VMO,
} object_type_t;

typedef struct object {
    /**
     * Optional null-terminated object name
     * used for debugging
     */
    const char* name;

    /**
     * The ref-count of the object
     */
    atomic_size_t ref_count;

    /**
     * Some flags about the object
     */
    object_flag_t flags;

    /**
     * The type of the object
     */
    object_type_t type;
} object_t;

static inline object_t* object_get(object_t* object) {
    atomic_fetch_add_explicit(&object->ref_count, 1, memory_order_relaxed);
    return object;
}

void object_put(object_t* object);
