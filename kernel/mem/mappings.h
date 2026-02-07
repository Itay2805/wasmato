#pragma once

#include "region.h"

/**
 * The upper half region
 */
extern region_t g_kernel_memory;

/**
 * The lower half region
 */
extern region_t g_user_memory;

/**
 * The different segments of the kernel
 */
extern region_t g_kernel_region;
extern region_t g_kernel_limine_requests_region;
extern region_t g_kernel_text_region;
extern region_t g_kernel_rodata_region;
extern region_t g_kernel_data_region;

/**
 * The direct map of the kernel
 */
extern region_t g_direct_map_region;

/**
 * The bitmap of the buddy allocator of the kernel
 */
extern region_t g_buddy_bitmap_region;
