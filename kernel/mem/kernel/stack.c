#include "stack.h"

#include "alloc.h"
#include "arch/paging.h"
#include "lib/string.h"
#include "mem/vmars.h"
#include "mem/internal/virt.h"
#include "sync/spinlock.h"

err_t stack_alloc(const char* name, size_t stack_size, void** stack) {
    err_t err = NO_ERROR;
    vmar_t* vmar = NULL;
    vmo_t* vmo = NULL;

    // allocate the range for the stack
    RETHROW(vmar_allocate(
        &g_upper_half_vmar,
        0, 0, stack_size + (PAGE_SIZE * 2),
        0,
        &vmar
    ));

    // setup the name
    vmar->object.name = "stack-with-guard";

    // create the vmo itself
    vmo = vmo_create(stack_size);
    CHECK_ERROR(vmo != NULL, ERROR_OUT_OF_MEMORY);
    vmo->object.name = name;

    // map as writeable and populate right away (lazy stacks are a curse that
    // I don't want to deal with).
    // we also want to map at a specific offset to ensure that we have
    // the guards between each of the sides
    void* mapped_addr = NULL;
    RETHROW(vmar_map(
        vmar,
        VMAR_MAP_POPULATE | VMAR_MAP_WRITE | VMAR_MAP_SPECIFIC,
        PAGE_SIZE,
        vmo, 0, stack_size, 0,
        &mapped_addr
    ));

    // and output it
    *stack = mapped_addr + stack_size;

cleanup:
    if (IS_ERROR(err)) {
        // if we failed we need to get rid of the ref
        if (vmo != NULL) {
            vmo_put(vmo);
        }

        if (vmar != NULL) {
            // unmap the region
            vmar_unmap(&g_upper_half_vmar, vmar->region.start, PAGES_TO_SIZE(vmar->region.page_count));
        }
    }

    // we don't need this object anymore
    if (vmar != NULL) vmar_put(vmar);

    return err;
}

