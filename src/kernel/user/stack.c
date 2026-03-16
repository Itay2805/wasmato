#include "stack.h"

#include "arch/intrin.h"
#include "mem/mappings.h"
#include "mem/vmar.h"
#include "lib/assert.h"
#include "lib/rbtree/rbtree.h"

LATE_RO bool g_shadow_stack_supported = false;

LATE_RO uintptr_t g_shadow_stack_thread_entry_thunk = 0;

err_t user_stack_alloc(stack_alloc_t* alloc, const char* name, size_t size) {
    err_t err = NO_ERROR;

    vmar_lock();

    // 1 page to each side for guard
    size_t total_pages = SIZE_TO_PAGES(size) + 2;

    // and another range with its own guard for the shadow stack
    if (g_shadow_stack_supported) {
        total_pages += SIZE_TO_PAGES(size) + 1;
    }

    size_t stack_offset = PAGE_SIZE;
    size_t shadow_stack_offset = PAGE_SIZE + ALIGN_UP(size, PAGE_SIZE) + PAGE_SIZE;

    // reserve the total region
    vmar_t* guard_region = vmar_reserve(&g_user_memory, total_pages, nullptr);
    CHECK_ERROR(guard_region != nullptr, ERROR_OUT_OF_MEMORY);

    // allocate the stack
    vmar_t* stack = vmar_allocate(guard_region, SIZE_TO_PAGES(size), guard_region->base + stack_offset);
    CHECK_ERROR(stack != nullptr, ERROR_OUT_OF_MEMORY);

    // allocate the shadow stack (if enabled)
    vmar_t* shadow_stack = nullptr;
    if (g_shadow_stack_supported) {
        shadow_stack = vmar_allocate(guard_region, SIZE_TO_PAGES(size), guard_region->base + shadow_stack_offset);
        CHECK_ERROR(shadow_stack != nullptr, ERROR_OUT_OF_MEMORY);
    }

    // lock both the guard and stack regions
    // so they can't be freed
    vmar_set_name(stack, "stack");
    stack->type = VMAR_TYPE_STACK;
    stack->locked = true;

    // lock the shadow stack and ensure its
    // marked as shadow stack
    if (shadow_stack != nullptr) {
        vmar_set_name(shadow_stack, "shadow-stack");
        shadow_stack->type = VMAR_TYPE_SHADOW_STACK;
        shadow_stack->alloc.protection = MAPPING_PROTECTION_RO;
        shadow_stack->locked = true;
    }

    // give the name to the top level entry
    vmar_set_user_name(guard_region, name);
    guard_region->locked = true;

    // return both stacks
    alloc->stack = vmar_end(stack) + 1;
    if (shadow_stack != nullptr) {
        void* ssp = vmar_end(shadow_stack) + 1 - 8;

        // push the thread_entry_thunk address, this is so
        // the scheduler can return into the new thread
        ssp -= 8;
        _wrussq(g_shadow_stack_thread_entry_thunk, ssp);

        // push the stack restore token, so we can even switch
        // to this shadow stack
        ssp -= 8;
        _wrussq(((uintptr_t)ssp + 8) | BIT0, ssp);

        alloc->shadow_stack = ssp;
    }

cleanup:
    if (IS_ERROR(err)) {
        if (guard_region != nullptr) {
            vmar_free(guard_region);
        }
    }

    vmar_unlock();

    return err;
}

void user_stack_free(void* ptr) {
    vmar_lock();

    // get the guard region
    vmar_t* vmar = vmar_find(&g_user_memory, ptr);
    ASSERT(vmar != nullptr);
    ASSERT(vmar->type == VMAR_TYPE_REGION);
    ASSERT(vmar->parent == &g_user_memory);

    // ensure that this looks like a stack guard region
    bool found_stack = false;
    bool found_shadow_stack = false;
    for (rb_node_t* node = rb_first(&vmar->region.root); node != nullptr; node = rb_next(node)) {
        vmar_t* child = rb_entry(node, vmar_t, node);
        if (child->type == VMAR_TYPE_STACK) {
            ASSERT(!found_stack);
            found_stack = true;
        } else if (child->type == VMAR_TYPE_SHADOW_STACK) {
            ASSERT(g_shadow_stack_supported);
            ASSERT(!found_shadow_stack);
            found_shadow_stack = true;
        } else {
            ASSERT(!"Invalid vmar type in stack region");
        }
    }

    // ensure we found the entries at all
    ASSERT(found_stack);
    if (g_shadow_stack_supported) {
        ASSERT(found_shadow_stack);
    }

    // free the entire region
    vmar_free(vmar);

    vmar_unlock();
}
