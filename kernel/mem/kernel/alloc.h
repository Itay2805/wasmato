#pragma once

#include <stddef.h>
#include <stdalign.h>

#include "lib/string.h"

#define alloc_type(type) \
    ({ \
        type* __ptr = mem_alloc(sizeof(type), alignof(type)); \
        if (__ptr != NULL) { \
            memset(__ptr, 0, sizeof(type)); \
        } \
        __ptr; \
    })


#define free_type(type, ptr) \
    do { \
        type* __ptr = ptr; \
        mem_free(__ptr, sizeof(type), alignof(type)); \
    } while (0)

#define alloc_array(type, count) \
    ({ \
        size_t __total_size = sizeof(type) * (count); \
        type* __ptr = mem_alloc(__total_size, alignof(type)); \
        if (__ptr != NULL) { \
            memset(__ptr, 0, __total_size); \
        } \
        __ptr; \
    })

#define realloc_array(type, ptr, old_count, new_count) \
    ({ \
        void* __ptr = ptr; \
        size_t __old_count = old_count; \
        size_t __new_count = new_count; \
        (type*)mem_realloc(__ptr, sizeof(type) * __old_count, alignof(type), sizeof(type) * __new_count); \
    })

#define free_array(type, ptr, count) \
    do { \
        type* __ptr = ptr; \
        size_t __count = count; \
        mem_free(__ptr, sizeof(type) * __count, alignof(type)); \
    } while(0)

void* mem_alloc(size_t size, size_t align);

void* mem_realloc(void* ptr, size_t old_size, size_t align, size_t new_size);

void mem_free(void* ptr, size_t size, size_t align);
