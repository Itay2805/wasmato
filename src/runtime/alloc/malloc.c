#include <stdint.h>

#include "alloc.h"
#include "malloc_impl.h"
#include "lib/assert.h"
#include "sync/mutex.h"
#include "uapi/page.h"
#include "uapi/syscall.h"

static struct {
    atomic_uint_fast64_t binmap;
    struct bin bins[64];
    mutex_t split_merge_lock;
} mal;

static inline void lock_bin(int i) {
    mutex_lock(&mal.bins[i].lock);
    if (!mal.bins[i].head)
        mal.bins[i].head = mal.bins[i].tail = BIN_TO_CHUNK(i);
}

static inline void unlock_bin(int i) {
    mutex_unlock(&mal.bins[i].lock);
}

static int first_set(uint64_t x) {
    return __builtin_ctzll(x);
}

static const unsigned char bin_tab[60] = {
    32, 33, 34, 35, 36, 36, 37, 37, 38, 38, 39, 39,
    40, 40, 40, 40, 41, 41, 41, 41, 42, 42, 42, 42, 43, 43, 43, 43,
    44, 44, 44, 44, 44, 44, 44, 44, 45, 45, 45, 45, 45, 45, 45, 45,
    46, 46, 46, 46, 46, 46, 46, 46, 47, 47, 47, 47, 47, 47, 47, 47,
};

static int bin_index(size_t x) {
    x = x / SIZE_ALIGN - 1;
    if (x <= 32) return x;
    if (x < 512) return bin_tab[x / 8 - 4];
    if (x > 0x1c00) return 63;
    return bin_tab[x / 128 - 4] + 16;
}

static int bin_index_up(size_t x) {
    x = x / SIZE_ALIGN - 1;
    if (x <= 32) return x;
    x--;
    if (x < 512) return bin_tab[x / 8 - 4] + 1;
    return bin_tab[x / 128 - 4] + 17;
}

/* Expand the heap in-place if brk can be used, or otherwise via mmap,
 * using an exponential lower bound on growth by mmap to make
 * fragmentation asymptotically irrelevant. The size argument is both
 * an input and an output, since the caller needs to know the size
 * allocated, which will be larger than requested due to page alignment
 * and mmap minimum size rules. The caller is responsible for locking
 * to prevent concurrent calls. */

static void *__expand_heap(size_t *pn) {
    static unsigned mmap_step;
    size_t n = *pn;

    if (n > SIZE_MAX / 2 - PAGE_SIZE) {
        return nullptr;
    }
    n += -n & PAGE_SIZE - 1;

    size_t min = (size_t) PAGE_SIZE << mmap_step / 2;
    if (n < min) n = min;
    void *area = sys_heap_alloc(SIZE_TO_PAGES(n));
    if (area == nullptr)
        return nullptr;
    *pn = n;
    mmap_step++;
    return area;
}

static struct chunk *expand_heap(size_t n) {
    static void *end;
    void *p;
    struct chunk *w;

    /* The argument n already accounts for the caller's chunk
     * overhead needs, but if the heap can't be extended in-place,
     * we need room for an extra zero-sized sentinel chunk. */
    n += SIZE_ALIGN;

    p = __expand_heap(&n);
    if (!p) return 0;

    /* If not just expanding existing space, we need to make a
     * new sentinel chunk below the allocated space. */
    if (p != end) {
        /* Valid/safe because of the prologue increment. */
        n -= SIZE_ALIGN;
        p = (char *) p + SIZE_ALIGN;
        w = MEM_TO_CHUNK(p);
        w->psize = 0 | C_INUSE;
    }

    /* Record new heap end and fill in footer. */
    end = (char *) p + n;
    w = MEM_TO_CHUNK(end);
    w->psize = n | C_INUSE;
    w->csize = 0 | C_INUSE;

    /* Fill in header, which may be new or may be replacing a
     * zero-size sentinel header at the old end-of-heap. */
    w = MEM_TO_CHUNK(p);
    w->csize = n | C_INUSE;

    return w;
}

static int adjust_size(size_t *n) {
    /* Result of pointer difference must fit in ptrdiff_t. */
    if (*n - 1 > PTRDIFF_MAX - SIZE_ALIGN - PAGE_SIZE) {
        if (*n) {
            return -1;
        } else {
            *n = SIZE_ALIGN;
            return 0;
        }
    }
    *n = (*n + OVERHEAD + SIZE_ALIGN - 1) & SIZE_MASK;
    return 0;
}

static void unbin(struct chunk *c, int i) {
    if (c->prev == c->next)
        atomic_fetch_and(&mal.binmap, ~(1ULL << i));
    c->prev->next = c->next;
    c->next->prev = c->prev;
    c->csize |= C_INUSE;
    NEXT_CHUNK(c)->psize |= C_INUSE;
}

static void bin_chunk(struct chunk *self, int i) {
    self->next = BIN_TO_CHUNK(i);
    self->prev = mal.bins[i].tail;
    self->next->prev = self;
    self->prev->next = self;
    if (self->prev == BIN_TO_CHUNK(i))
        atomic_fetch_or(&mal.binmap, 1ULL << i);
}

static void trim(struct chunk *self, size_t n) {
    size_t n1 = CHUNK_SIZE(self);
    struct chunk *next, *split;

    if (n >= n1 - DONTCARE) return;

    next = NEXT_CHUNK(self);
    split = (void *) ((char *) self + n);

    split->psize = n | C_INUSE;
    split->csize = n1 - n;
    next->psize = n1 - n;
    self->csize = n | C_INUSE;

    int i = bin_index(n1 - n);
    lock_bin(i);

    bin_chunk(split, i);

    unlock_bin(i);
}

void* mem_alloc(size_t n) {
    int i, j;
    uint64_t mask;

    if (adjust_size(&n) < 0)
        return nullptr;

    if (n > MMAP_THRESHOLD) {
        size_t len = n + OVERHEAD + PAGE_SIZE - 1 & -PAGE_SIZE;
        char *base = sys_heap_alloc(SIZE_TO_PAGES(len));
        if (base == nullptr)
            return nullptr;

        struct chunk* c = (void *) (base + SIZE_ALIGN - OVERHEAD);
        c->csize = len - (SIZE_ALIGN - OVERHEAD);
        c->psize = SIZE_ALIGN - OVERHEAD;
        return CHUNK_TO_MEM(c);
    }

    i = bin_index_up(n);
    struct chunk* c = nullptr;
    if (i < 63 && (mal.binmap & (1ULL << i))) {
        lock_bin(i);
        c = mal.bins[i].head;
        if (c != BIN_TO_CHUNK(i) && CHUNK_SIZE(c) - n <= DONTCARE) {
            unbin(c, i);
            unlock_bin(i);
            return CHUNK_TO_MEM(c);
        }
        unlock_bin(i);
    }
    mutex_lock(&mal.split_merge_lock);
    for (mask = mal.binmap & -(1ULL << i); mask; mask -= (mask & -mask)) {
        j = first_set(mask);
        lock_bin(j);
        c = mal.bins[j].head;
        if (c != BIN_TO_CHUNK(j)) {
            unbin(c, j);
            unlock_bin(j);
            break;
        }
        unlock_bin(j);
    }
    if (!mask) {
        c = expand_heap(n);
        if (!c) {
            mutex_unlock(&mal.split_merge_lock);
            return nullptr;
        }
    }
    trim(c, n);
    mutex_unlock(&mal.split_merge_lock);
    return CHUNK_TO_MEM(c);
}

void* mem_realloc(void *p, size_t n) {
    struct chunk *self, *next;
    size_t n0;
    void *new;

    if (!p) return mem_alloc(n);

    if (adjust_size(&n) < 0) return 0;

    self = MEM_TO_CHUNK(p);
    n0 = CHUNK_SIZE(self);

    if (n <= n0 && n0 - n <= DONTCARE) return p;

    if (IS_MMAPPED(self)) {
        size_t extra = self->psize;
        size_t oldlen = n0 + extra;
        size_t newlen = n + extra;
        /* Crash on realloc of freed chunk */
        ASSERT((extra & 1) == 0);
        if (newlen < PAGE_SIZE && (new = mem_alloc(n - OVERHEAD))) {
            n0 = n;
            goto copy_free_ret;
        }
        newlen = (newlen + PAGE_SIZE - 1) & -PAGE_SIZE;
        if (oldlen == newlen) return p;
        goto copy_realloc;
    }

    next = NEXT_CHUNK(self);

    /* Crash on corrupted footer (likely from buffer overflow) */
    ASSERT (next->psize == self->csize);

    if (n < n0) {
        int i = bin_index_up(n);
        int j = bin_index(n0);
        if (i < j && (mal.binmap & (1ULL << i)))
            goto copy_realloc;
        struct chunk *split = (void *) ((char *) self + n);
        self->csize = split->psize = n | C_INUSE;
        split->csize = next->psize = n0 - n | C_INUSE;
        __bin_chunk(split);
        return CHUNK_TO_MEM(self);
    }

    mutex_lock(&mal.split_merge_lock);

    size_t nsize = next->csize & C_INUSE ? 0 : CHUNK_SIZE(next);
    if (n0 + nsize >= n) {
        int i = bin_index(nsize);
        lock_bin(i);
        if (!(next->csize & C_INUSE)) {
            unbin(next, i);
            unlock_bin(i);
            next = NEXT_CHUNK(next);
            self->csize = next->psize = n0 + nsize | C_INUSE;
            trim(self, n);
            mutex_unlock(&mal.split_merge_lock);
            return CHUNK_TO_MEM(self);
        }
        unlock_bin(i);
    }
    mutex_unlock(&mal.split_merge_lock);

copy_realloc:
    /* As a last resort, allocate a new chunk and copy to it. */
    new = mem_alloc(n - OVERHEAD);
    if (!new) return 0;
copy_free_ret:
    memcpy(new, p, (n < n0 ? n : n0) - OVERHEAD);
    mem_free(CHUNK_TO_MEM(self));
    return new;
}

void __bin_chunk(struct chunk *self) {
    struct chunk *next = NEXT_CHUNK(self);

    /* Crash on corrupted footer (likely from buffer overflow) */
    ASSERT (next->psize == self->csize);

    mutex_lock(&mal.split_merge_lock);

    size_t osize = CHUNK_SIZE(self), size = osize;

    /* Since we hold split_merge_lock, only transition from free to
     * in-use can race; in-use to free is impossible */
    size_t psize = self->psize & C_INUSE ? 0 : CHUNK_PSIZE(self);
    size_t nsize = next->csize & C_INUSE ? 0 : CHUNK_SIZE(next);

    if (psize) {
        int i = bin_index(psize);
        lock_bin(i);
        if (!(self->psize & C_INUSE)) {
            struct chunk *prev = PREV_CHUNK(self);
            unbin(prev, i);
            self = prev;
            size += psize;
        }
        unlock_bin(i);
    }
    if (nsize) {
        int i = bin_index(nsize);
        lock_bin(i);
        if (!(next->csize & C_INUSE)) {
            unbin(next, i);
            next = NEXT_CHUNK(next);
            size += nsize;
        }
        unlock_bin(i);
    }

    int i = bin_index(size);
    lock_bin(i);

    self->csize = size;
    next->psize = size;
    bin_chunk(self, i);
    mutex_unlock(&mal.split_merge_lock);

    /* Replace middle of large chunks with fresh zero pages */
    if (size > RECLAIM && (size ^ (size - osize)) > size - osize) {
        uintptr_t a = (uintptr_t) self + SIZE_ALIGN + PAGE_SIZE - 1 & -PAGE_SIZE;
        uintptr_t b = (uintptr_t) next - SIZE_ALIGN & -PAGE_SIZE;
        // TODO: madvice don't need, does it even matter if we won't allow overcommit?
        // __madvise((void *) a, b - a, MADV_DONTNEED);
    }

    unlock_bin(i);
}

static void unmap_chunk(struct chunk *self) {
    size_t extra = self->psize;
    char *base = (char *) self - extra;
    size_t len = CHUNK_SIZE(self) + extra;
    /* Crash on double free */
    ASSERT ((extra & 1) == 0);
    sys_heap_free(base);
}

void mem_free(void *p) {
    if (!p) return;

    struct chunk *self = MEM_TO_CHUNK(p);

    if (IS_MMAPPED(self))
        unmap_chunk(self);
    else
        __bin_chunk(self);
}
