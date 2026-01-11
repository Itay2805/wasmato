#include "mappings.h"

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
};

#define VMAR_STATIC_INIT(_name) \
    (vmar_t){ \
        .object = { \
            .name = _name, \
            .ref_count = 1, \
            .flags = OBJECT_STATIC, \
            .type = OBJECT_TYPE_VMAR \
        } \
    }

#define PHYSICAL_VMO_STATIC_INIT(_name) \
    { \
        .object = { \
            .name = _name, \
            .ref_count = 1, \
            .flags = OBJECT_STATIC, \
            .type = OBJECT_TYPE_VMO \
        }, \
        .type = VMO_TYPE_PHYSICAL, \
        .cache_policy = VMO_CACHE_POLICY_CACHED, \
        .page_count = 0, \
        .pages = { 0 } \
    }

#define VIRTUAL_VMO_STATIC_INIT(_name, size) \
    { \
        .object = { \
            .name = _name, \
            .ref_count = 1, \
            .flags = OBJECT_STATIC, \
            .type = OBJECT_TYPE_VMO \
        }, \
        .type = VMO_TYPE_PHYSICAL, \
        .cache_policy = VMO_CACHE_POLICY_CACHED, \
        .page_count = SIZE_TO_PAGES(size), \
        .pages = { 0 } \
    }

vmar_t g_code_vmar = VMAR_STATIC_INIT("code");
vmar_t g_kernel_vmar = VMAR_STATIC_INIT("kernel");

vmo_t g_kernel_limine_requests_vmo = PHYSICAL_VMO_STATIC_INIT("limine_requests");
vmo_t g_kernel_text_vmo = PHYSICAL_VMO_STATIC_INIT("text");
vmo_t g_kernel_rodata_vmo = PHYSICAL_VMO_STATIC_INIT("rodata");
vmo_t g_kernel_data_vmo = PHYSICAL_VMO_STATIC_INIT("data");

heap_vmo_t g_heap_vmo = {
    .vmo = {
        .object = {
            .name = "heap",
            .ref_count = 1,
            .flags = OBJECT_STATIC,
            .type = OBJECT_TYPE_VMO
        },
        .type = VMO_TYPE_NORMAL,
        .cache_policy = VMO_CACHE_POLICY_CACHED,
        .page_count = SIZE_TO_PAGES(HEAP_SIZE)
    }
};

vmar_t g_direct_map_vmar = VMAR_STATIC_INIT("direct-map");
vmar_t g_buddy_bitmap_vmar = VMAR_STATIC_INIT("buddy-bitmap");
