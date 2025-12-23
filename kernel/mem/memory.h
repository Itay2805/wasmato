#pragma once

/**
 * Direct map offset, static in memory, no KASLR please
 */
#define DIRECT_MAP_OFFSET       0xFFFF800000000000ULL

/**
 * Convert direct map pointers as required
 */
#define PHYS_TO_DIRECT(x) (void*)((uintptr_t)(x) + DIRECT_MAP_OFFSET)
#define DIRECT_TO_PHYS(x) (uintptr_t)((uintptr_t)(x) - DIRECT_MAP_OFFSET)

// page size is 4k
#define PAGE_SHIFT  12
#define PAGE_MASK   ((1 << PAGE_SHIFT) - 1)
#define PAGE_SIZE   (1 << PAGE_SHIFT)

#define SIZE_TO_PAGES(size)   (((size) >> PAGE_SHIFT) + (((size) & PAGE_MASK) ? 1 : 0))
#define PAGES_TO_SIZE(pages)  ((pages) << PAGE_SHIFT)
