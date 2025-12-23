#include "alloc.h"

#include <lib/list.h>
#include <lib/string.h>
#include <sync/spinlock.h>
#include <lib/rbtree/rbtree.h>

#include "memory.h"
#include "phys.h"

void init_alloc(void) {

}

void* mem_alloc(size_t size, size_t align) {
    return NULL;
}

void* mem_realloc(void* ptr, size_t old_size, size_t align, size_t new_size) {
    return NULL;
}

void mem_free(void* ptr, size_t size, size_t align) {

}

