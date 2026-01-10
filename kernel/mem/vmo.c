#include "vmo.h"

#include "arch/paging.h"
#include "internal/direct.h"
#include "internal/phys.h"
#include "kernel/alloc.h"
#include "lib/string.h"

vmo_t* vmo_create(uint64_t size) {
    size_t page_count = DIV_ROUND_UP(size, PAGE_SIZE);
    size_t vmo_size = sizeof(vmo_t) + page_count * sizeof(uint64_t);
    vmo_t* vmo = mem_alloc(vmo_size, alignof(vmo_t));
    if (vmo == NULL) {
        return NULL;
    }
    memset(vmo, 0, vmo_size);

    // setup the object
    vmo->object.type = OBJECT_TYPE_VMO;
    vmo->object.ref_count = 1;

    // by default have a normal caching policy
    vmo->type = VMO_TYPE_NORMAL;
    vmo->cache_policy = VMO_CACHE_POLICY_CACHED;

    // set all the pages, we are going to allocate on demand
    vmo->page_count = page_count;

    return vmo;
}

vmo_t* vmo_create_physical(uint64_t physical_address, size_t size) {
    size_t vmo_size = sizeof(vmo_t) + sizeof(uint64_t);
    vmo_t* vmo = mem_alloc(vmo_size, alignof(vmo_t));
    if (vmo == NULL) {
        return NULL;
    }
    memset(vmo, 0, vmo_size);

    // setup the object
    vmo->object.type = OBJECT_TYPE_VMO;
    vmo->object.ref_count = 1;

    // assume its device caching
    vmo->type = VMO_TYPE_PHYSICAL;
    vmo->cache_policy = VMO_CACHE_POLICY_UNCACHED;

    // we have a single "page" for the entire range
    vmo->page_count = 1;
    vmo->pages[0] = (physical_address >> 12) | VMO_PAGE_PRESENT;

    return vmo;
}

void vmo_destroy(vmo_t* vmo) {
    // if we are the owner free all the pages
    // we assume that unmap has already taken care of the actual
    // mappings
    if (vmo->type == VMO_TYPE_NORMAL) {
        for (int i = 0; i < vmo->page_count; i++) {
            uint64_t frame = vmo->pages[i];
            if (frame & VMO_PAGE_PRESENT) {
                void* ptr = phys_to_direct((frame & VMO_PAGE_FRAME_MASK) << 12);
                phys_free(ptr, PAGE_SIZE);
            }
        }
    }

    size_t vmo_size = sizeof(vmo_t) + vmo->page_count * sizeof(uint64_t);
    mem_free(vmo, vmo_size, alignof(vmo_t));
}