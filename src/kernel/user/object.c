#include "object.h"

#include "irq/irq.h"
#include "lib/assert.h"
#include "lib/except.h"
#include "lib/atomic.h"
#include "lib/list.h"


kernel_object_t* kernel_object_get(kernel_object_t* object) {
    atomic_fetch_add_explicit(&object->ref_count, 1, memory_order_acquire);
    return object;

}

void kernel_object_put(kernel_object_t* object) {
    size_t ref_count = atomic_fetch_sub_explicit(&object->ref_count, 1, memory_order_release);
    ASSERT(ref_count >= 1);

    if (ref_count != 1) {
        return;
    }

    atomic_fence_acquire();

    switch (object->type) {
        // TODO: free the object in here
        case KERNEL_OBJECT_TYPE_IRQ: {
            irq_free(containerof(object, irq_t, object));
        } break;

        default:
            ASSERT(0, "Invalid kernel object type %d", object->type);
    }

}
