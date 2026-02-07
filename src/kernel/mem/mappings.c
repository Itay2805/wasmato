#include "mappings.h"

#include "arch/paging.h"

region_t g_kernel_memory ={
    .name = "kernel-memory",
    .root = RB_ROOT,
    .base = (void*)0xFFFF800000000000,
    .page_count = SIZE_128TB / PAGE_SIZE,
    .type = REGION_TYPE_DEFAULT,
    .pinned = true,
};

region_t g_user_memory = {
    .name = "user-memory",
    .root = RB_ROOT,
    .base = (void*)BASE_64KB,
    .page_count = (SIZE_128TB - BASE_64KB) / PAGE_SIZE,
    .type = REGION_TYPE_DEFAULT,
    .pinned = true,
};

region_t g_kernel_region = {
    .name = "kernel",
    .root = RB_ROOT,
    .base = (void*)0xffffffff80000000,
    .page_count = SIZE_2GB / PAGE_SIZE,
    .type = REGION_TYPE_DEFAULT,
    .pinned = true,
};

region_t g_runtime_region = {
    .name = "runtime",
    .root = RB_ROOT,
    .base = NULL,
    .page_count = 0,
    .type = REGION_TYPE_DEFAULT,
    .pinned = true
};

region_t g_runtime_heap_region = {
    .name = "jit",
    .root = RB_ROOT,
    .base = NULL,
    .page_count = 0,
    .type = REGION_TYPE_DEFAULT,
    .pinned = true
};

extern char __kernel_limine_requests_base[];
extern char __kernel_text_base[];
extern char __kernel_rodata_base[];
extern char __kernel_data_base[];

extern char __kernel_limine_requests_page_count[];
extern char __kernel_text_page_count[];
extern char __kernel_rodata_page_count[];
extern char __kernel_data_page_count[];

region_t g_kernel_limine_requests_region = {
    .name = "limine_requests",
    .base = __kernel_limine_requests_base,
    .page_count = (size_t)__kernel_limine_requests_page_count,
    .type = REGION_TYPE_MAPPING_SPECIAL,
    .cache_policy = MAPPING_CACHE_POLICY_CACHED,
    .protection = MAPPING_PROTECTION_RO,
    .locked = true,
    .pinned = true,
};

region_t g_kernel_text_region = {
    .name = "text",
    .base = __kernel_text_base,
    .page_count = (size_t)__kernel_text_page_count,
    .type = REGION_TYPE_MAPPING_SPECIAL,
    .cache_policy = MAPPING_CACHE_POLICY_CACHED,
    .protection = MAPPING_PROTECTION_RX,
    .locked = true,
    .pinned = true,
};

region_t g_kernel_rodata_region = {
    .name = "rodata",
    .base = __kernel_rodata_base,
    .page_count = (size_t)__kernel_rodata_page_count,
    .type = REGION_TYPE_MAPPING_SPECIAL,
    .cache_policy = MAPPING_CACHE_POLICY_CACHED,
    .protection = MAPPING_PROTECTION_RO,
    .locked = true,
    .pinned = true,
};

region_t g_kernel_data_region = {
    .name = "data",
    .base = __kernel_data_base,
    .page_count = (size_t)__kernel_data_page_count,
    .type = REGION_TYPE_MAPPING_SPECIAL,
    .cache_policy = MAPPING_CACHE_POLICY_CACHED,
    .protection = MAPPING_PROTECTION_RW,
    .locked = true,
    .pinned = true,
};

region_t g_direct_map_region = {
    .name = "direct-map",
    .type = REGION_TYPE_MAPPING_PHYS,
    .phys = 0,
    .cache_policy = MAPPING_CACHE_POLICY_CACHED,
    .protection = MAPPING_PROTECTION_RW,
    .locked = true,
    .pinned = true,
};

region_t g_buddy_bitmap_region = {
    .name = "buddy-bitmap",
    .type = REGION_TYPE_MAPPING_SPECIAL,
    .cache_policy = MAPPING_CACHE_POLICY_CACHED,
    .protection = MAPPING_PROTECTION_RW,
    .locked = true,
    .pinned = true,
};

