#include "object.h"
#include "alloc/alloc.h"
#include "lib/assert.h"
#include "lib/atomic.h"
#include "lib/except.h"
#include "lib/syscall.h"
#include <stdatomic.h>
#include <stdint.h>
#include "lib/string.h"
#include "sync/mutex.h"
#include "uapi/wait.h"
#include "wasi/wasip1.h"

void object_init(object_t* object) {
    memset(object, 0, sizeof(*object));
    object->ref_count = 1;
}


object_t* object_get(object_t* object) {
    atomic_fetch_add(&object->ref_count, 1);
    return object;
}

void object_put(object_t* object) {
    if (atomic_fetch_sub_explicit(&object->ref_count, 1, memory_order_release) != 1) {
        return;
    }

    // We just went from 1 -> 0. Synchronize with all other droppers
    // before touching the object.
    atomic_fence_acquire();

    // at this point it must have a zero handle count
    ASSERT(object->handle_count == 0);

    // call the callback if any
    if (object->free != nullptr) {
        object->free(object);
    }

    // free the object
    mem_free(object);
}

object_t* object_handle_get(object_t* object) {
    atomic_fetch_add(&object->handle_count, 1);
    return object;
}

void object_handle_put(object_t* object) {
    if (atomic_fetch_sub_explicit(&object->ref_count, 1, memory_order_release) != 1) {
        return;
    }

    // We just went from 1 -> 0. Synchronize with all other droppers
    // before touching the object.
    atomic_fence_acquire();

    // at this point it must have a zero handle count
    ASSERT(object->handle_count == 0);

    // signal that we are closed
    object_signal(object, SIGNAL_CLOSED);

    // call the callback if any
    if (object->close != nullptr) {
        object->close(object);
    }

    // free the object
    object_put(object);
}

void object_signal(object_t* object, uint32_t set_mask) {
    // set the bits that we signaled
    uint32_t old = atomic_fetch_or_explicit(&object->signals, set_mask, memory_order_release);

    if ((old & set_mask) != set_mask) {
        // the old value will only have less bits than we have right now, so if it does
        // not have the exact same bits, it means we set some bits, so we should notify
        // the waiters
        sys_atomic_notify(&object->signals, set_mask, 0);
    }
}

void object_clear_signal(object_t* object, uint32_t clear_mask) {
    // clear the signal, the acquire pairs with the setter's release 
    atomic_fetch_and_explicit(&object->signals, ~clear_mask, memory_order_acquire);
}

uint32_t object_prepare_wait(object_t* object, uint32_t signals, wait_entry_t* entry) {
    // first check if we already have a valid signal
    uint32_t pending = atomic_load_acquire(&object->signals);
    if ((pending & signals) != 0) {
        return pending;
    }

    // not signaled, set the entry
    entry->key = &object->signals;
    entry->key_size = WAIT_KEY_UINT32;
    entry->mask = signals;
    entry->old = pending;

    return 0;
}

err_t object_wait_one(object_t* object, uint32_t signals, uint64_t deadline, uint32_t* observed) {
    err_t err = WASI_ERRNO_SUCCESS;

    wait_entry_t entry = {
        .key = &object->signals,
        .key_size = WAIT_KEY_UINT32,
        .mask = signals,
    };

    for (;;) {
        // perform a first check on which signals are pending
        uint32_t pending = atomic_load_acquire(&object->signals);
        if ((pending & signals) != 0) {
            // TODO: should we mask it or not?
            *observed = pending;
            break;
        }

        // perform the wait 
        wait_status_t status = sys_atomic_wait(&entry, 1, deadline);
        CHECK_ERROR(status != WAIT_STATUS_OUT_OF_MEMORY, WASI_ERRNO_NOMEM);
    }

cleanup:
    return err;
}
