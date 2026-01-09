#include "stack.h"

#include "arch/paging.h"
#include "lib/string.h"
#include "mem/internal/virt.h"
#include "sync/spinlock.h"

err_t stack_alloc(size_t stack_size, void** out_stack_start, void** out_stack_end) {
    err_t err = NO_ERROR;
    void* ptr = NULL;

    CHECK_FAIL();

cleanup:
    return err;
}

