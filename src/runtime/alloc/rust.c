
#include "lib/string.h"

void* rust_platform_alloc(size_t size, size_t align) {
    return NULL;
}

void* rust_platform_realloc(void* ptr, size_t old_size, size_t align, size_t new_size) {
    return NULL;
}

void rust_platform_free(void* ptr, size_t size, size_t align) {

}
