#pragma once

#include <stddef.h>
#include <stdalign.h>

#include "sync/spinlock.h"
#include "lib/list.h"
#include "lib/string.h"

typedef struct mem_alloc {
    /**
     * Linked list of all the slabs
     * in the system
     */
    list_entry_t link;

    /**
     * List of slabs with available objects
     */
    list_t partial;

    /**
     * List of full slabs
     */
    list_t full;

    /**
     * List of empty slabs, available as cache
     */
    list_t empty;

    /**
     * Lock to protect the allocator
     */
    irq_spinlock_t lock;

    /**
     * Size of a single slab
     */
    uint16_t slab_size;

    /**
     * The objects in each slab
     */
    uint16_t objects_per_slab;

    /**
     * The stride of each object
     */
    uint16_t object_stride;

    /**
     * The size of each object
     */
    uint16_t object_size;

    /**
     * The object's alignment
     */
    uint16_t objet_align;
} mem_alloc_t;

void mem_alloc_init(mem_alloc_t* alloc, size_t size, size_t align);

void* mem_alloc(mem_alloc_t* alloc);

void* mem_calloc(mem_alloc_t* alloc);

void mem_free(mem_alloc_t* slab, void* p);
