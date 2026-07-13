#include "file.h"
#include "alloc/alloc.h"
#include "lib/atomic.h"
#include "lib/list.h"
#include "lib/log.h"
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

    if (file->ops->free != nullptr) {
        file->ops->free(file);
    }

    mem_free(file);
}
