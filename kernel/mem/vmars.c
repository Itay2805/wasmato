#include "vmars.h"

vmar_t g_upper_half_vmar = {
    .object = {
        .name = "upper-half",
        .ref_count = 1,
        .flags = OBJECT_STATIC,
        .type = OBJECT_TYPE_VMAR
    },
    .region = {
        // for the upper half just have the entire region
        .start = (void*)0xFFFF800000000000,
        .page_count = SIZE_128TB / PAGE_SIZE,
        .object = &g_upper_half_vmar.object,
    },
    .bump_alloc_max = (void*)0xFFFF800000000000,
};

vmar_t g_lower_half_vmar = {
    .object = {
        .name = "lower-half",
        .ref_count = 1,
        .flags = OBJECT_STATIC,
        .type = OBJECT_TYPE_VMAR
    },
    .region = {
        // for the lower half leave the lowest 4gb unmapped at all times
        // and go right up to the top of it
        .start = (void*)SIZE_4GB,
        .page_count = (SIZE_128TB - SIZE_4GB) / PAGE_SIZE,
        .object = &g_lower_half_vmar.object,
    },
    .bump_alloc_max = (void*)SIZE_4GB,
};

#define VMAR_STATIC_INIT(_name) \
    { \
        .object = { \
            .name = _name, \
            .ref_count = 1, \
            .flags = OBJECT_STATIC, \
            .type = OBJECT_TYPE_VMAR \
        } \
    }

vmar_t g_code_vmar = VMAR_STATIC_INIT("code");
vmar_t g_kernel_vmar = VMAR_STATIC_INIT("kernel");
vmar_t g_kernel_limine_requests_vmar = VMAR_STATIC_INIT("limine_requests");
vmar_t g_kernel_text_vmar = VMAR_STATIC_INIT("text");
vmar_t g_kernel_rodata_vmar = VMAR_STATIC_INIT("rodata");
vmar_t g_kernel_data_vmar = VMAR_STATIC_INIT("data");

vmar_t g_heap_vmar = VMAR_STATIC_INIT("heap");

vmar_t g_direct_map_vmar = VMAR_STATIC_INIT("direct-map");
vmar_t g_buddy_bitmap_vmar = VMAR_STATIC_INIT("buddy-bitmap");
