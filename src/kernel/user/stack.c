#include "stack.h"

#include "mem/mappings.h"
#include "mem/vmar.h"
#include "lib/assert.h"
#include "lib/rbtree/rbtree.h"

LATE_RO bool g_shadow_stack_supported;

void* user_stack_get_shadow(void* stack, size_t stack_size) {
    // TODO: maybe use a vmar lookup or something?
    return stack + PAGE_SIZE + ALIGN_UP(stack_size, PAGE_SIZE);
}

err_t user_stack_alloc(stack_alloc_t* alloc, void* name, size_t size) {
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
    stack->name = "stack";
    stack->type = VMAR_TYPE_STACK;
    stack->locked = true;

    // lock the shadow stack and ensure its
    // marked as shadow stack
    if (shadow_stack != nullptr) {
        shadow_stack->name = "shadow-stack";
        shadow_stack->type = VMAR_TYPE_SHADOW_STACK;
        shadow_stack->alloc.protection = MAPPING_PROTECTION_RO;
        shadow_stack->locked = true;
    }

    // give the name to the top level entry
    guard_region->name = name;
    guard_region->locked = true;

    // return both stacks
    alloc->stack = vmar_end(stack) + 1;
    if (shadow_stack != nullptr) {
        alloc->shadow_stack = vmar_end(shadow_stack) + 1 - 8;
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

    // TODO: verify this is a stack region

    // free the entire mapping
    vmar_free(vmar->base);

    vmar_unlock();
}
