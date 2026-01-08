#pragma once

#include <stdbool.h>

#include "arch/paging.h"

//
// ========================================================================================================
//     Start addr    |     End addr     |  Size   | VM area description
// ========================================================================================================
//                   |                  |         |
//  0000000000000000 | 00007fffffffffff | ~128 TB | wasm linear memory spaces
// __________________|__________________|_________|________________________________________________________
//                   |                  |         |
//  0000800000000000 | ffff7fffffffffff |  ~16 EB | ... huge, almost 63 bits wide hole of non-canonical
//                   |                  |         |     virtual memory addresses up to the -128 TB
//                   |                  |         |     starting offset of kernel mappings.
// __________________|__________________|_________|________________________________________________________
//  ffff800000000000 | ffffbfffffffffff |   64 TB | direct mapping of all physical memory
//  ffffc00000000000 | ffffc07fffffffff |  0.5 TB | ... guard hole
//  ffffc08000000000 | ffffc17fffffffff |    1 TB | virtual buddy bitmap
//  ffffc18000000000 | ffffc1ffffffffff |  0.5 TB | ... guard hole
//  ffffc20000000000 | ffffc2ffffffffff |    1 TB | kernel stacks
//  ffffc30000000000 | ffffc37fffffffff |  0.5 TB | ... guard hole
//  ffffc38000000000 | ffffc387ffffffff |   32 TB | kernel heap
//  ffffc38800000000 | ffffc407ffffffff |  0.5 TB | ... guard hole
//  ffffc40800000000 | ffffffff7fffffff | 28.5 TB | ... unused hole
//  ffffffff80000000 | ffffffff9fffffff |  512 MB | kernel text mapping
//  ffffffffa0000000 | ffffffffffffffff | 1520 MB | Wasm JIT mapping space
// __________________|__________________|_________|___________________________________________________________
//

#define DIRECT_MAP_START            ((void*)0xffff800000000000ULL)
#define DIRECT_MAP_SIZE             (SIZE_64TB)
#define DIRECT_MAP_END              (DIRECT_MAP_START + DIRECT_MAP_SIZE)

#define PHYS_BUDDY_BITMAP_START     ((uint8_t*)0xffffc00000000000)
#define PHYS_BUDDY_BITMAP_SIZE      (SIZE_1TB)
#define PHYS_BUDDY_BITMAP_END       (PHYS_BUDDY_BITMAP_START + PHYS_BUDDY_BITMAP_SIZE)

#define STACKS_ADDR_START           ((void*)0xffffc20000000000)
#define STACKS_ADDR_LENGTH          (SIZE_1TB)
#define STACKS_ADDR_END             (STACKS_ADDR_START + STACKS_ADDR_LENGTH)

#define HEAP_ADDR_START             ((void*)0xffffc38000000000)
#define HEAP_ADDR_LENGTH            (SIZE_32TB)
#define HEAP_ADDR_END               (HEAP_ADDR_START + HEAP_ADDR_LENGTH)

#define JIT_ADDR_START              ((void*)0xffffffffa0000000)
#define JIT_ADDR_LENGTH             (SIZE_1GB + SIZE_512MB)
#define JIT_ADDR_END                (JIT_ADDR_START + JIT_ADDR_LENGTH)

/**
 * Convert direct map pointers as required
 */
#define PHYS_TO_DIRECT(x) (void*)((uintptr_t)(x) + DIRECT_MAP_START)
#define DIRECT_TO_PHYS(x) (uintptr_t)((uintptr_t)(x) - (uintptr_t)DIRECT_MAP_START)

__attribute__((always_inline))
static inline void unlock_direct_map(void) {
    asm volatile ("stac" ::: "memory");
}

__attribute__((always_inline))
static inline void lock_direct_map(void) {
    asm volatile ("clac" ::: "memory");
}
