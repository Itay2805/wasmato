#include "stack.h"

#include "mappings.h"
#include "vmar.h"
#include "lib/assert.h"
#include "lib/rbtree/rbtree.h"

void* user_stack_alloc(void* name, size_t size) {
    vmar_lock();

    // 1 page to each side for guard
    size_t total_pages = SIZE_TO_PAGES(size) + 2;

    // reserve the total region
    vmar_t* guard_region = vmar_reserve(&g_user_memory, total_pages, nullptr);
    if (guard_region == nullptr) {
        vmar_unlock();
        return nullptr;
    }

    // allocate the stack
    vmar_t* stack = vmar_allocate(guard_region, SIZE_TO_PAGES(size), guard_region->base + PAGE_SIZE);
    if (stack == nullptr) {
        vmar_free(guard_region->base);
        vmar_unlock();
        return nullptr;
    }

    // lock both the guard and stack regions
    // so they can't be freed
    stack->name = name;
    stack->pinned = true;
    stack->locked = true;

    guard_region->name = "stack-guard";
    guard_region->pinned = true;
    guard_region->locked = true;

    stack->type = VMAR_TYPE_STACK;

    void* ptr = vmar_end(stack) + 1;

    vmar_unlock();

    return ptr;
}

void user_stack_free(void* ptr) {
    vmar_lock();

    // get the guard region
    vmar_t* vmar = vmar_find(&g_user_memory, ptr);
    ASSERT(vmar != nullptr);
    ASSERT(vmar->type == VMAR_TYPE_REGION);

    // get the stack region
    vmar_t* stack = rb_entry_safe(rb_first(&vmar->region.root), vmar_t, node);
    ASSERT(stack != nullptr);
    ASSERT(rb_next(&stack->node) == nullptr);
    ASSERT(stack->type == VMAR_TYPE_STACK);

    // unlock so we can free them
    stack->locked = false;
    vmar->locked = false;

    // free the entire mapping
    vmar_free(vmar->base);

    vmar_unlock();
}
