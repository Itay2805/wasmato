#pragma once

#include "arch/paging.h"

/**
 * Direct map, contains mappings of physical memory
 */
#define DIRECT_MAP_START            ((void*)0xFFFF800000000000ULL)
#define DIRECT_MAP_SIZE             (1ULL << 46)
#define DIRECT_MAP_END              (DIRECT_MAP_START + DIRECT_MAP_SIZE)

/**
 * Buddy allocator bitmap
 */
#define PHYS_BUDDY_BITMAP_START     ((uint8_t*)DIRECT_MAP_END)
#define PHYS_BUDDY_BITMAP_SIZE      ((DIRECT_MAP_SIZE / PAGE_SIZE) / 8)
#define PHYS_BUDDY_BITMAP_END       (PHYS_BUDDY_BITMAP_START + PHYS_BUDDY_BITMAP_SIZE)

/**
 * Stacks
 */
#define STACKS_ADDR_START           ((void*)PHYS_BUDDY_BITMAP_END)
#define STACKS_ADDR_LENGTH          SIZE_1GB
#define STACKS_ADDR_END             (STACKS_ADDR_START + STACKS_ADDR_LENGTH)

/**
 * Convert direct map pointers as required
 */
#define PHYS_TO_DIRECT(x) (void*)((uintptr_t)(x) + DIRECT_MAP_START)
#define DIRECT_TO_PHYS(x) (uintptr_t)((uintptr_t)(x) - (uintptr_t)DIRECT_MAP_START)
