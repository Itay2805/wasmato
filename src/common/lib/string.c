
#include <stddef.h>
#include <stdint.h>
#include <cpuid.h>

#include "defs.h"

size_t strlen(const char* s) {
    const char *a = s;
    for (; *s; s++) {}
    return s - a;
}

int strcmp(const char* l, const char* r) {
    for (; *l == *r && *l; l++, r++) {}
    return *(unsigned char *)l - *(unsigned char *)r;
}
