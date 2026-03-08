#pragma once

#include "vmar.h"

/**
 * The upper half region
 */
LATE_RO extern vmar_t g_kernel_memory;

/**
 * The lower half region
 */
extern vmar_t g_user_memory;
extern vmar_t g_runtime_region;

/**
 * The different segments of the kernel
 */
LATE_RO extern vmar_t g_kernel_region;
LATE_RO extern vmar_t g_kernel_limine_requests_region;
LATE_RO extern vmar_t g_kernel_init_text_region;
LATE_RO extern vmar_t g_kernel_init_data_region;
LATE_RO extern vmar_t g_kernel_text_region;
LATE_RO extern vmar_t g_kernel_rodata_region;
LATE_RO extern vmar_t g_kernel_late_rodata_region;
LATE_RO extern vmar_t g_kernel_data_region;

/**
 * The direct map of the kernel
 */
LATE_RO extern vmar_t g_direct_map_region;

/**
 * The bitmap of the buddy allocator of the kernel
 */
LATE_RO extern vmar_t g_buddy_bitmap_region;
