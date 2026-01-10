#pragma once
#include "mem/vmars.h"

__attribute__((always_inline))
static inline void unlock_direct_map(void) {
    asm volatile ("stac" ::: "memory");
}

__attribute__((always_inline))
static inline void lock_direct_map(void) {
    asm volatile ("clac" ::: "memory");
}

static inline void* phys_to_direct(uint64_t ptr) {
    return g_direct_map_vmar.region.start + ptr;
}

static inline uint64_t direct_to_phys(void* ptr) {
    return ptr - g_direct_map_vmar.region.start;
}
