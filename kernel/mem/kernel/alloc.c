#include "alloc_internal.h"
#include "alloc.h"

#include "valloc.h"
#include "arch/paging.h"
#include "lib/assert.h"
#include "lib/string.h"

static atomic_uint_fast64_t m_alloc_binmap;
static alloc_bin_t m_alloc_bins[64];
static irq_spinlock_t m_alloc_split_merge_lock;

static inline bool lock_bin(int i) {
	bool irq_state = irq_spinlock_acquire(&m_alloc_bins[i].lock);
	if (!m_alloc_bins[i].head) {
		m_alloc_bins[i].head = m_alloc_bins[i].tail = BIN_TO_CHUNK(i);
	}
	return irq_state;
}

static inline void unlock_bin(int i, bool irq_state) {
	irq_spinlock_release(&m_alloc_bins[i].lock, irq_state);
}

static int first_set(uint64_t x) {
	return __builtin_ctzll(x);
}

static const unsigned char m_alloc_bin_tab[60] = {
	            32,33,34,35,36,36,37,37,38,38,39,39,
	40,40,40,40,41,41,41,41,42,42,42,42,43,43,43,43,
	44,44,44,44,44,44,44,44,45,45,45,45,45,45,45,45,
	46,46,46,46,46,46,46,46,47,47,47,47,47,47,47,47,
};

static int alloc_bin_index(size_t x) {
	x = x / SIZE_ALIGN - 1;
	if (x <= 32) return x;
	if (x < 512) return m_alloc_bin_tab[x/8-4];
	if (x > 0x1c00) return 63;
	return m_alloc_bin_tab[x/128-4] + 16;
}

static int alloc_bin_index_up(size_t x) {
	x = x / SIZE_ALIGN - 1;
	if (x <= 32) return x;
	x--;
	if (x < 512) return m_alloc_bin_tab[x/8-4] + 1;
	return m_alloc_bin_tab[x/128-4] + 17;
}

static void* alloc_expand_heap(size_t* pn) {
    static void* brk;
    static unsigned mmap_step;
    size_t n = *pn;

    if (n > SIZE_MAX / 2 - PAGE_SIZE) {
        return NULL;
    }
    n += -n & PAGE_SIZE-1;

    if (!brk) {
        brk = valloc_expand(0);
    }

    if (n < (void*)SIZE_MAX - brk) {
        void* newbrk = valloc_expand(n);
        void* oldbrk = brk;
        *pn = newbrk - oldbrk;
        brk = newbrk;
        return oldbrk;
    }

    size_t min = (size_t)PAGE_SIZE << mmap_step / 2;
    if (n < min) n = min;
    void* area = valloc_alloc(n);
    if (area == NULL) {
        return NULL;
    }

    *pn = n;
    mmap_step++;
    return area;

}

static alloc_chunk_t* alloc_chunk(size_t n) {
	static void* end;

	// The argument n already accounts for the caller's chunk
	// overhead needs, but if the heap can't be extended in-place,
	// we need room for an extra zero-sized sentinel chunk.
	n += SIZE_ALIGN;

	void* p = alloc_expand_heap(&n);
	if (p == NULL) {
		return NULL;
	}

	// If not just expanding existing space, we need to make a
	// new sentinel chunk below the allocated space.
	alloc_chunk_t* w = NULL;
	if (p != end) {
		/* Valid/safe because of the prologue increment. */
		n -= SIZE_ALIGN;
		p = (char *)p + SIZE_ALIGN;
		w = MEM_TO_CHUNK(p);
		w->psize = 0 | C_INUSE;
	}

	// Record new heap end and fill in footer.
	end = (char *)p + n;
	w = MEM_TO_CHUNK(end);
	w->psize = n | C_INUSE;
	w->csize = 0 | C_INUSE;

	// Fill in header, which may be new or may be replacing a
	// zero-size sentinel header at the old end-of-heap.
	w = MEM_TO_CHUNK(p);
	w->csize = n | C_INUSE;

	return w;
}

static bool alloc_adjust_size(size_t* n) {
	/* Result of pointer difference must fit in ptrdiff_t. */
	if (*n - 1 > PTRDIFF_MAX - SIZE_ALIGN - PAGE_SIZE) {
		if (*n) {
			return false;
		} else {
			*n = SIZE_ALIGN;
			return true;
		}
	}
	*n = (*n + OVERHEAD + SIZE_ALIGN - 1) & SIZE_MASK;
	return true;
}

static void alloc_unbin(alloc_chunk_t* c, int i) {
	if (c->prev == c->next) {
		m_alloc_binmap &= ~(1ULL << i);
	}
	c->prev->next = c->next;
	c->next->prev = c->prev;
	c->csize |= C_INUSE;
	NEXT_CHUNK(c)->psize |= C_INUSE;
}

static void alloc_bin(alloc_chunk_t* self, int i) {
	self->next = BIN_TO_CHUNK(i);
	self->prev = m_alloc_bins[i].tail;
	self->next->prev = self;
	self->prev->next = self;
	if (self->prev == BIN_TO_CHUNK(i)) {
		m_alloc_binmap |= 1ULL << i;
	}
}

static void alloc_trim(alloc_chunk_t* self, size_t n) {
	size_t n1 = CHUNK_SIZE(self);
	if (n >= n1 - DONTCARE) {
		return;
	}

	alloc_chunk_t* next = NEXT_CHUNK(self);
	alloc_chunk_t* split = (void *)((char *)self + n);

	split->psize = n | C_INUSE;
	split->csize = n1-n;
	next->psize = n1-n;
	self->csize = n | C_INUSE;

	int i = alloc_bin_index(n1-n);
	bool irq_state = lock_bin(i);
	alloc_bin(split, i);
	unlock_bin(i, irq_state);
}

static void* alloc_internal(size_t n) {
	if (!alloc_adjust_size(&n)) {
		return NULL;
	}

	if (n > MMAP_THRESHOLD) {
		size_t len = n + OVERHEAD + PAGE_SIZE - 1 & -PAGE_SIZE;
		char* base = valloc_alloc(len);
		if (base == NULL) {
			return NULL;
		}

		alloc_chunk_t* c = (void *)(base + SIZE_ALIGN - OVERHEAD);
		c->csize = len - (SIZE_ALIGN - OVERHEAD);
		c->psize = SIZE_ALIGN - OVERHEAD;
		return CHUNK_TO_MEM(c);
	}

	int i = alloc_bin_index_up(n);
	if (i < 63 && (m_alloc_binmap & (1ULL<<i))) {
		bool irq_state = lock_bin(i);
		alloc_chunk_t* c = m_alloc_bins[i].head;
		if (c != BIN_TO_CHUNK(i) && CHUNK_SIZE(c)-n <= DONTCARE) {
			alloc_unbin(c, i);
			unlock_bin(i, irq_state);
			return CHUNK_TO_MEM(c);
		}
		unlock_bin(i, irq_state);
	}

	bool irq_state = irq_spinlock_acquire(&m_alloc_split_merge_lock);
	uint64_t mask = 0;
	alloc_chunk_t* c = NULL;
	for (mask = m_alloc_binmap & -(1ULL << i); mask; mask -= (mask & -mask)) {
		int j = first_set(mask);
		lock_bin(j);
		c = m_alloc_bins[j].head;
		if (c != BIN_TO_CHUNK(j)) {
			alloc_unbin(c, j);
			unlock_bin(j, false);
			break;
		}
		unlock_bin(j, false);
	}

	if (c == NULL) {
		c = alloc_chunk(n);
		if (!c) {
			irq_spinlock_release(&m_alloc_split_merge_lock, irq_state);
			return NULL;
		}
	}

	alloc_trim(c, n);
	irq_spinlock_release(&m_alloc_split_merge_lock, irq_state);
	return CHUNK_TO_MEM(c);
}

static void alloc_bin_chunk(alloc_chunk_t* self) {
	alloc_chunk_t* next = NEXT_CHUNK(self);

	// Crash on corrupted footer (likely from buffer overflow)
	ASSERT(next->psize == self->csize);

	bool irq_state = irq_spinlock_acquire(&m_alloc_split_merge_lock);

	size_t osize = CHUNK_SIZE(self), size = osize;

	// Since we hold split_merge_lock, only transition from free to
	// in-use can race; in-use to free is impossible
	size_t psize = self->psize & C_INUSE ? 0 : CHUNK_PSIZE(self);
	size_t nsize = next->csize & C_INUSE ? 0 : CHUNK_SIZE(next);

	if (psize != 0) {
		int i = alloc_bin_index(psize);
		lock_bin(i);
		if (!(self->psize & C_INUSE)) {
			alloc_chunk_t* prev = PREV_CHUNK(self);
			alloc_unbin(prev, i);
			self = prev;
			size += psize;
		}
		unlock_bin(i, false);
	}

	if (nsize != 0) {
		int i = alloc_bin_index(nsize);
		lock_bin(i);
		if (!(next->csize & C_INUSE)) {
			alloc_unbin(next, i);
			next = NEXT_CHUNK(next);
			size += nsize;
		}
		unlock_bin(i, false);
	}

	int i = alloc_bin_index(size);
	lock_bin(i);

	self->csize = size;
	next->psize = size;
	alloc_bin(self, i);
	irq_spinlock_release(&m_alloc_split_merge_lock, false);

	unlock_bin(i, irq_state);
}

static void alloc_unmap_chunk(alloc_chunk_t* self) {
	size_t extra = self->psize;
	char *base = (char *)self - extra;
	size_t len = CHUNK_SIZE(self) + extra;
	ASSERT((extra & 1) == 0);
	valloc_free(base, len);
}

void* mem_alloc(size_t size, size_t align) {
	if ((align & -align) != align) {
		return NULL;
	}

	if (size > SIZE_MAX - align) {
		return NULL;
	}

	// small allocations are already naturally aligned
	if (align <= SIZE_ALIGN) {
		return alloc_internal(size);
	}

	// allocate more and align correctly
	void* mem = alloc_internal(size + align - 1);
	if (mem == NULL) {
		return mem;
	}

	void* new = (void*)ALIGN_UP((uintptr_t)mem, align);
	if (new == mem) return mem;

	alloc_chunk_t* c = MEM_TO_CHUNK(mem);
	alloc_chunk_t* n = MEM_TO_CHUNK(new);

	if (IS_MMAPPED(c)) {
		// Apply difference between aligned and original
		// address to the "extra" field of mmapped chunk.
		n->psize = c->psize + (new-mem);
		n->csize = c->csize - (new-mem);
		return new;
	}

	alloc_chunk_t* t = NEXT_CHUNK(c);

	// Split the allocated chunk into two chunks. The aligned part
	// that will be used has the size in its footer reduced by the
	// difference between the aligned and original addresses, and
	// the resulting size copied to its header. A new header and
	// footer are written for the split-off part to be freed.
	n->psize = c->csize = C_INUSE | (new-mem);
	n->csize = t->psize -= new-mem;

	alloc_bin_chunk(c);
	return new;
}

void* mem_realloc(void* ptr, size_t old_size, size_t align, size_t new_size) {
	// TODO: something more optimized when possible
	void* new = mem_alloc(new_size, align);
	if (new != NULL && ptr != NULL) {
		memcpy(new, ptr, old_size);
		mem_free(ptr, old_size, align);
	}
	return new;
}

void mem_free(void* ptr, size_t size, size_t align) {
	if (ptr == NULL)
		return;

	alloc_chunk_t* self = MEM_TO_CHUNK(ptr);

	if (IS_MMAPPED(self)) {
		alloc_unmap_chunk(self);
	} else {
		alloc_bin_chunk(self);
	}
}
