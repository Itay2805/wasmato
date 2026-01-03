#pragma once
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The kernel memory map
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// 0xFFFF8000_00000000 - 0xFFFFFF7F_FFFFFFFF: Direct map
// 0xFFFFFF80_00000000 - 0xFFFFFF80_3FFFFFFF: Thread stacks
// 0xFFFFFFFF_80000000 - 0xFFFFFFFF_FFFFFFFF: Kernel (2GB)
//
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///
/**
 * Direct map offset, static in memory, no KASLR please
 */
#define DIRECT_MAP_OFFSET       0xFFFF800000000000ULL

#define STACKS_ADDR_START       0xFFFFFF8000000000ULL
#define STACKS_ADDR_LENGTH      SIZE_1GB
#define STACKS_ADDR_END         (STACKS_ADDR_START + STACKS_ADDR_LENGTH)

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
