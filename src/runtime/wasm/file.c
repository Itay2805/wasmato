#include "file.h"
#include "alloc/alloc.h"
#include "lib/assert.h"
#include "lib/atomic.h"
#include "lib/syscall.h"

file_t* file_get(file_t* file) {
    atomic_fetch_add_explicit(&file->ref_count, 1, memory_order_acquire);
    return file;
}

void file_put(file_t* file) {
    size_t ref_count = atomic_fetch_sub_explicit(&file->ref_count, 1, memory_order_release);
    if (ref_count != 1) {
        return;
    }

    atomic_fence_acquire();

    // make sure the state makes sense
    ASSERT(file->use_count == 0);
    ASSERT(file->signals & FILE_SIGNAL_CLOSED);

    if (file->ops->free != nullptr) {
        file->ops->free(file);
    }

    mem_free(file);
}

file_t* file_use_get(file_t* file) {
    size_t use_count = atomic_fetch_add_explicit(&file->use_count, 1, memory_order_acquire);
    return file;
}

void file_use_put(file_t* file) {
    size_t use_count = atomic_fetch_sub_explicit(&file->use_count, 1, memory_order_release);
    if (use_count != 1) {
        return;
    }

    atomic_fence_acquire();

    // this was the last use, we can close the file itself
    if (file->ops->close != nullptr) {
        file->ops->close(file);
    }

    // notify all waiters that we are dead
    atomic_fetch_or_explicit(&file->signals, FILE_SIGNAL_CLOSED, memory_order_release);
    sys_atomic_notify(&file->signals, FILE_SIGNAL_CLOSED, 0);

    // we can remove the ref that comes from 
    // having a use count
    file_put(file);

    mem_free(file);

}
