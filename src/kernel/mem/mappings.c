#include "mappings.h"

#include "arch/paging.h"
#include "lib/defs.h"

vmar_t g_kernel_memory ={
    .name = "kernel-memory",
    .base = (void*)0xFFFF800000000000,
    .page_count = SIZE_128TB / PAGE_SIZE,
    .type = VMAR_TYPE_REGION,
    .pinned = true,
    .region = {
        .root = RB_ROOT
    }
};

vmar_t g_user_memory = {
    .name = "user-memory",
    .base = (void*)BASE_64KB,
    .page_count = (SIZE_128TB - BASE_64KB) / PAGE_SIZE,
    .type = VMAR_TYPE_REGION,
    .pinned = true,
    .region = {
        .root = RB_ROOT
    }
};

vmar_t g_kernel_region = {
    .name = "kernel",
    .base = (void*)0xffffffff80000000,
    .page_count = SIZE_2GB / PAGE_SIZE,
    .type = VMAR_TYPE_REGION,
    .pinned = true,
    .region = {
        .root = RB_ROOT
    }
};

vmar_t g_runtime_region = {
    .name = "runtime",
    .base = NULL,
    .page_count = 0,
    .type = VMAR_TYPE_REGION,
    .pinned = true,
    .region = {
        .root = RB_ROOT
    }
};

extern char __kernel_limine_requests_base[];
extern char __kernel_text_base[];
extern char __kernel_rodata_base[];
extern char __kernel_data_base[];

extern char __kernel_limine_requests_page_count[];
extern char __kernel_text_page_count[];
extern char __kernel_rodata_page_count[];
extern char __kernel_data_page_count[];

vmar_t g_kernel_limine_requests_region = {
    .name = "limine_requests",
    .base = __kernel_limine_requests_base,
    .page_count = (size_t)__kernel_limine_requests_page_count,
    .type = VMAR_TYPE_SPECIAL,
    .locked = true,
    .pinned = true,
};

vmar_t g_kernel_text_region = {
    .name = "text",
    .base = __kernel_text_base,
    .page_count = (size_t)__kernel_text_page_count,
    .type = VMAR_TYPE_SPECIAL,
    .locked = true,
    .pinned = true,
    .alloc = {
        .protection = MAPPING_PROTECTION_RX
    }
};

vmar_t g_kernel_rodata_region = {
    .name = "rodata",
    .base = __kernel_rodata_base,
    .page_count = (size_t)__kernel_rodata_page_count,
    .type = VMAR_TYPE_SPECIAL,
    .locked = true,
    .pinned = true,
    .alloc = {
        .protection = MAPPING_PROTECTION_RO
    }
};

vmar_t g_kernel_data_region = {
    .name = "data",
    .base = __kernel_data_base,
    .page_count = (size_t)__kernel_data_page_count,
    .type = VMAR_TYPE_SPECIAL,
    .locked = true,
    .pinned = true,
    .alloc = {
        .protection = MAPPING_PROTECTION_RW
    }
};

vmar_t g_direct_map_region = {
    .name = "direct-map",
    .type = VMAR_TYPE_PHYS,
    .locked = true,
    .pinned = true,
    .phys = {
        .phys = 0
    },
};

vmar_t g_buddy_bitmap_region = {
    .name = "buddy-bitmap",
    .type = VMAR_TYPE_SPECIAL,
    .locked = true,
    .pinned = true,
};

