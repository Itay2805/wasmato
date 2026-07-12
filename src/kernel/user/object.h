#pragma once


#include <stdint.h>
#include <stdatomic.h>

typedef enum kernel_object_type : uint8_t {
    KERNEL_OBJECT_TYPE_IRQ,
} kernel_object_type_t;

typedef struct kernel_object {
    /**
     * Ref count to protect the object from being freed
     */
    atomic_size_t ref_count;

    /**
     * The kernel object type
     */
    kernel_object_type_t type;
} kernel_object_t;

/**
 * Increment the kernel object reference
 */
kernel_object_t* kernel_object_get(kernel_object_t* object);

/**
 * Release a kernel object handle
 */
void kernel_object_put(kernel_object_t* object);
