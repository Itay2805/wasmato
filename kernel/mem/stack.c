#include "stack.h"

#include <lib/defs.h>
#include <lib/list.h>
#include <sync/spinlock.h>

#include "memory.h"
#include "phys.h"
#include "virt.h"
#include "lib/except.h"
#include "lib/string.h"

static void* m_stack_watermark = (void*)STACKS_ADDR_START;
static irq_spinlock_t m_stack_lock = IRQ_SPINLOCK_INIT;

err_t stack_alloc(void** out_stack_start, void** out_stack_end) {
    err_t err = NO_ERROR;
    void* ptr = NULL;
    size_t stack_num_pages = 0;

    bool irq_state = irq_spinlock_acquire(&m_stack_lock);

    // TODO: free list of stacks that are not used right now

    // ensure we have space
    CHECK_ERROR(m_stack_watermark + SIZE_32KB <= (void*)STACKS_ADDR_END, ERROR_OUT_OF_MEMORY);

    // allocate from the watermark
    void* new_watermark = m_stack_watermark;
    ptr = new_watermark;
    new_watermark += SIZE_32KB;

    // we are going to leave a page for a guard
    void* stack_end = ptr + PAGE_SIZE;
    void* stack_start = ptr + SIZE_32KB;
    STATIC_ASSERT((SIZE_32KB % PAGE_SIZE) == 0);

    // allocate the entire stack right away
    stack_num_pages = SIZE_TO_PAGES(stack_start - stack_end);
    RETHROW(virt_alloc(stack_end, stack_num_pages));

    // zero the entire stack
    memset(stack_end, 0, stack_start - stack_end);

    // we are done
    if (out_stack_start != NULL) *out_stack_start = stack_start;
    if (out_stack_end != NULL) *out_stack_end = stack_end;
    m_stack_watermark = new_watermark;

cleanup:
    irq_spinlock_release(&m_stack_lock, irq_state);

    return err;
}

