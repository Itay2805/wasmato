#include "stack.h"

#include "virt.h"
#include "arch/intrin.h"
#include "mem/mappings.h"
#include "mem/vmar.h"
#include "lib/assert.h"
#include "lib/rbtree/rbtree.h"
#include "thread/thread.h"

LATE_RO bool g_shadow_stack_supported = false;

err_t stack_alloc(stack_alloc_t* alloc, const char* name, size_t size, stack_alloc_flag_t flags) {
    err_t err = NO_ERROR;
    bool user = flags & STACK_ALLOC_USER;
    vmar_t* top_vmar = user ? &g_user_memory : &g_kernel_memory;

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
    vmar_t* guard_region = vmar_reserve(top_vmar, total_pages, nullptr);
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
    vmar_set_name(guard_region, name);
    guard_region->locked = true;

    // return both stacks
    alloc->stack = vmar_end(stack) + 1;
    if (shadow_stack != nullptr) {
        void* ssp = vmar_end(shadow_stack) + 1 - 8;

        // if this is a kernel stack setup the supervisor ssp token
        // for non-interrupt stacks we also need to mark the stack
        // as to be prepared for thread entry
        if (!user) {
            bool thread_entry = (flags & STACK_ALLOC_IST) == 0;
            RETHROW(virt_setup_shadow_stack_token(ssp, thread_entry));
        }

        alloc->shadow_stack = ssp;
    }

    // for kernel stacks always pre-fault the stacks, this ensures no nested
    // page faults or anything alike can happen, we have small stacks for
    // the kernel so that should be fine
    if (!user) {
        for (void* addr = stack->base; addr < vmar_end(stack); addr += PAGE_SIZE) {
            RETHROW(virt_handle_page_fault((uintptr_t)addr, 0));
        }

        if (shadow_stack != nullptr) {
            for (void *addr = shadow_stack->base; addr < vmar_end(shadow_stack); addr += PAGE_SIZE) {
                RETHROW(virt_handle_page_fault((uintptr_t)addr, IA32_PF_EC_SHSTK));
            }
        }
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

void stack_free(void* ptr, bool user) {
    vmar_t* top_vmar = user ? &g_user_memory : &g_kernel_memory;

    vmar_lock();

    // get the guard region
    vmar_t* vmar = vmar_find(top_vmar, ptr);
    ASSERT(vmar != nullptr);
    ASSERT(vmar->type == VMAR_TYPE_REGION);
    ASSERT(vmar->parent == top_vmar);

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
    } else {
        ASSERT(!found_shadow_stack);
    }

    // free the entire region
    vmar_free(vmar);

    vmar_unlock();
}
