#pragma once

#include <stddef.h>
#include <stdalign.h>

#define alloc_type(type) \
    (type*)mem_alloc(sizeof(type), alignof(type))

#define free_type(type, ptr) \
    mem_free(ptr, sizeof(type), alignof(type))

#define alloc_array(type, count) \
    (type*)mem_alloc(sizeof(type) * (count), alignof(type))

#define realloc_array(type, ptr, old_count, new_count) \
    ({ \
        void* __ptr = ptr; \
        size_t __old_count = old_count; \
        size_t __new_count = new_count; \
        (type*)mem_realloc(__ptr, sizeof(type) * __old_count, alignof(type), sizeof(type) * __new_count); \
    })

#define free_array(type, ptr, count) \
    do { \
        void* __ptr = ptr; \
        size_t __count = count; \
        mem_free(__ptr, sizeof(type) * (count), alignof(type)); \
    } while(0)

void init_alloc(void);

void* mem_alloc(size_t size, size_t align);

void* mem_realloc(void* ptr, size_t old_size, size_t align, size_t new_size);

void mem_free(void* ptr, size_t size, size_t align);
