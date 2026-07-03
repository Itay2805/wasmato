#include "random.h"
#include "arch/intrin.h"
#include "lib/assert.h"
#include "lib/defs.h"
#include "lib/pcpu.h"
#include <immintrin.h>
#include <limits.h>
#include <stdint.h>
#include <immintrin.h>

static INIT_CODE inline unsigned long long fmix64(unsigned long long k) {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return k;
}

static INIT_CODE bool rdrand64(void) {
    for (int i = 0; i < 10; i++) {
        unsigned long long v;
        if (_rdrand64_step(&v) && v != 0 && v != ULLONG_MAX) {
            return v;
        }
        cpu_relax();
    }
    ASSERT(!"Failed to produce boot random");
}

INIT_CODE OMIT_SP void boot_random_fill(void* data, size_t size) {
    uint8_t* p = (uint8_t*)data;
    while (size != 0) {
        // we add a bit of mixing to the rdrand for fun and profit, it doesn't practically 
        // change much, but hopefully if there is a firmware with a bug it will at least give 
        // something that is not completely constant
        unsigned long long w = fmix64(rdrand64() ^ __builtin_ia32_rdtsc());
        size_t n = size < sizeof(w) ? size : sizeof(w);
        for (size_t i = 0; i < n; i++) {
            p[i] = (uint8_t)(w >> (i * 8));
        }
        p += n;
        size -= n;
    }
}
