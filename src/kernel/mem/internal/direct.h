#pragma once
#include "mem/mappings.h"

static inline void* phys_to_direct(uint64_t ptr) {
    return g_direct_map_region.base + ptr;
}

static inline uint64_t direct_to_phys(void* ptr) {
    return ptr - g_direct_map_region.base;
}
