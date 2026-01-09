#include "object.h"

#include "lib/assert.h"
#include "mem/vmar.h"

void vmar_destroy(vmar_t* vmar);

void object_put(object_t* object) {
    size_t ref_count = atomic_fetch_sub_explicit(&object->ref_count, 1, memory_order_acq_rel);
    if (ref_count == 1) {
        // ensure this is not a static object, otherwise we have a double put
        ASSERT((object->flags & OBJECT_STATIC) == 0);

        switch (object->type) {
            case OBJECT_TYPE_VMAR: vmar_destroy(containerof(object, vmar_t, object)); break;
            default: ASSERT(!"Invalid object type"); break;
        }
    }
}
