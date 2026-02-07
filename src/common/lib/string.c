
#include <stddef.h>
#include <stdint.h>
#include <cpuid.h>

#include "defs.h"

void* memset(void* s, int c, size_t n) {
    // NOTE: we assume that fast short rep stosb is supported, meaning that
    //       0-128 length strings should be fast
    void* d = s;
    asm volatile (
        "rep stosb"
        : "+D"(s), "+c"(n)
        : "a"((unsigned char)c)
        : "memory"
    );
    return d;
}

__attribute__((always_inline))
static inline void __rep_movsb(void* dest, const void* src, size_t n) {
    asm volatile (
        "rep movsb"
        : "+D"(dest), "+S"(src), "+c"(n)
        :
        : "memory"
    );
}

void* memcpy(void* restrict dest, const void* restrict src, size_t n) {
    // fast path for zero length
    // TODO: patch away if we have fast zero-length rep movsb
    if (UNLIKELY(n == 0))
        return dest;

    __rep_movsb(dest, src, n);
    return dest;
}

void* memmove(void* dest, const void* src, size_t n) {
    // fast path for zero length or the same exact buffer
    if (UNLIKELY(n == 0) || (dest == src)) {
        return dest;
    }

    void* d = dest;

    if (src < dest && dest < src + n) {
        asm volatile (
            "std\n"
            "rep movsb\n"
            "cld\n"
            : "+D"(dest), "+S"(src), "+c"(n)
            :
            : "memory"
        );
    } else {
        // not overlapping, use a normal copy
        __rep_movsb(dest, src, n);
    }

    return d;
}

int memcmp(const void* vl, const void* vr, size_t n) {
    const unsigned char* l = vl;
    const unsigned char* r = vr;
    for (; n && *l == *r; n--, l++, r++) {
    }
    return n ? *l - *r : 0;
}

size_t strlen(const char* s) {
    const char *a = s;
    for (; *s; s++) {}
    return s - a;
}

int strcmp(const char* l, const char* r) {
    for (; *l == *r && *l; l++, r++) {}
    return *(unsigned char *)l - *(unsigned char *)r;
}
