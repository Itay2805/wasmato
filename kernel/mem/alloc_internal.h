#pragma once

#include "sync/spinlock.h"

typedef struct alloc_chunk {
	size_t psize, csize;
	struct alloc_chunk *next, *prev;
} alloc_chunk_t;

typedef struct alloc_bin {
	irq_spinlock_t lock;
	struct alloc_chunk *head;
	struct alloc_chunk *tail;
} alloc_bin_t;

#define SIZE_ALIGN		(4 * sizeof(size_t))
#define SIZE_MASK		(-SIZE_ALIGN)
#define OVERHEAD		(2 * sizeof(size_t))
#define MMAP_THRESHOLD	(0x1c00 * SIZE_ALIGN)
#define DONTCARE		16
#define RECLAIM			163840

#define CHUNK_SIZE(c)	((c)->csize & -2)
#define CHUNK_PSIZE(c)	((c)->psize & -2)
#define PREV_CHUNK(c)	((alloc_chunk_t*)((char*)(c) - CHUNK_PSIZE(c)))
#define NEXT_CHUNK(c)	((alloc_chunk_t*)((char*)(c) + CHUNK_SIZE(c)))
#define MEM_TO_CHUNK(p) (alloc_chunk_t*)((char*)(p) - OVERHEAD)
#define CHUNK_TO_MEM(c) (void*)((char*)(c) + OVERHEAD)
#define BIN_TO_CHUNK(i) (MEM_TO_CHUNK(&m_alloc_bins[i].head))

#define C_INUSE			((size_t)1)

#define IS_MMAPPED(c)	!((c)->csize & (C_INUSE))
