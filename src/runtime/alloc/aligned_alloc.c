#include <stdint.h>

#include "alloc.h"

#include "malloc_impl.h"
#include "lib/string.h"

void* mem_alloc_aligned(size_t len, size_t align) {
    unsigned char *mem, *new;

    if ((align & -align) != align) {
        return nullptr;
    }

    if (len > SIZE_MAX - align) {
        return nullptr;
    }

    if (align <= SIZE_ALIGN)
        return mem_alloc(len);

    if (!(mem = mem_alloc(len + align - 1)))
        return 0;

    new = (void *) ((uintptr_t) mem + align - 1 & -align);
    if (new == mem) return mem;

    struct chunk *c = MEM_TO_CHUNK(mem);
    struct chunk *n = MEM_TO_CHUNK(new);

    if (IS_MMAPPED(c)) {
        /* Apply difference between aligned and original
         * address to the "extra" field of mmapped chunk. */
        n->psize = c->psize + (new - mem);
        n->csize = c->csize - (new - mem);
        return new;
    }

    struct chunk *t = NEXT_CHUNK(c);

    /* Split the allocated chunk into two chunks. The aligned part
     * that will be used has the size in its footer reduced by the
     * difference between the aligned and original addresses, and
     * the resulting size copied to its header. A new header and
     * footer are written for the split-off part to be freed. */
    n->psize = c->csize = C_INUSE | (new - mem);
    n->csize = t->psize -= new - mem;

    __bin_chunk(c);
    return new;
}
