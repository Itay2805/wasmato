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
extern vmar_t g_kernel_limine_requests_vmar;
extern vmar_t g_kernel_text_vmar;
extern vmar_t g_kernel_rodata_vmar;
extern vmar_t g_kernel_data_vmar;

/**
 * VMARs that represent the direct map of the kernel
 */
extern vmar_t g_direct_map_vmar;
extern vmar_t g_buddy_bitmap_vmar;

/**
 * The kernel heap
 */
extern vmar_t g_heap_vmar;
