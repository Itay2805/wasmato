#pragma once
#include "vmar.h"

/**
 * The VMAR that represents the higher half of the address space
 */
extern vmar_t g_upper_half_vmar;

/**
 * The VMAR that represents the lower half of the address space
 */
extern vmar_t g_lower_half_vmar;

/**
 * The VMAR that represents the last 2gb of memory, where the kernel
 * and jit code can live easily
 */
extern vmar_t g_code_vmar;

/**
 * VMARs that represent the kernel itself
 */
extern vmar_t g_kernel_vmar;

extern vmo_t g_kernel_limine_requests_vmo;
extern vmo_t g_kernel_text_vmo;
extern vmo_t g_kernel_rodata_vmo;
extern vmo_t g_kernel_data_vmo;

#define HEAP_SIZE   SIZE_4GB

typedef struct heap_vmo {
    vmo_t vmo;
    uint64_t pages[SIZE_TO_PAGES(HEAP_SIZE)];
} heap_vmo_t;

/**
 * The kernel heap
 */
extern heap_vmo_t g_heap_vmo;

/**
 * VMARs that represent the direct map of the kernel
 */
extern vmar_t g_direct_map_vmar;
extern vmar_t g_buddy_bitmap_vmar;
