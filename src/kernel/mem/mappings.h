#pragma once

#include "vmar.h"

/**
 * The upper half region
 */
extern vmar_t g_kernel_memory;

/**
 * The lower half region
 */
extern vmar_t g_user_memory;
extern vmar_t g_runtime_region;

/**
 * The different segments of the kernel
 */
extern vmar_t g_kernel_region;
extern vmar_t g_kernel_limine_requests_region;
extern vmar_t g_kernel_text_region;
extern vmar_t g_kernel_rodata_region;
extern vmar_t g_kernel_data_region;

/**
 * The direct map of the kernel
 */
extern vmar_t g_direct_map_region;

/**
 * The bitmap of the buddy allocator of the kernel
 */
extern vmar_t g_buddy_bitmap_region;
