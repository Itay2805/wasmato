#include "alloc_internal.h"
#include "alloc.h"

#include "arch/paging.h"
#include "lib/assert.h"
#include "lib/string.h"
#include "mem/mappings.h"
#include "mem/internal/phys.h"

typedef struct free_node {
    struct free_node* next;
} free_node_t;

typedef struct slab {
    /**
     * Link into the allocator's link list of slabs
     */
    list_entry_t link;

    /**
     * The allocator the slab belongs to
     */
    mem_alloc_t* alloc;

    /**
     * The free-list of the slab
     */
    free_node_t* free;

    /**
     * Objects in use in the slab
     */
    uint16_t in_use;

    /**
     * Total amount of objects in the slab
     */
    uint16_t total;

    /**
     * The alignment of objects in the slab
     */
    uint16_t align;
} slab_t;

static inline slab_t* object_to_slab(void* p) {
    return ALIGN_DOWN(p, PAGE_SIZE);
}

static slab_t* slab_create(mem_alloc_t* alloc) {
    slab_t* slab = phys_alloc(alloc->slab_size);
    if (slab == NULL) {
        return NULL;
    }

    // setup the metadata
    slab->alloc = alloc;
    slab->align = alloc->objet_align;
    slab->free = NULL;
    slab->in_use = 0;
    slab->total = alloc->objects_per_slab;

    // link all the objects into the linked list
    void* area = (void*)slab + ALIGN_UP(sizeof(slab_t), slab->align);
    for (int i = 0; i < alloc->objects_per_slab; i++) {
        free_node_t* node = area + alloc->object_stride * i;
        node->next = slab->free;
        slab->free = node;
    }

    return slab;
}

/**
 * Linked list of all the allocators, to be used in case of OOM
 */
static list_t m_allocators = LIST_INIT(&m_allocators);

void mem_alloc_init(mem_alloc_t* alloc, size_t size, size_t align) {
    ASSERT(size != 0);
    ASSERT(align != 0);

    ASSERT(size <= UINT16_MAX);
    ASSERT(align <= UINT16_MAX);

    // calculate stride size
    size_t stride = ALIGN_UP(MAX(sizeof(free_node_t), size), align);
    ASSERT(stride <= UINT16_MAX);
    ASSERT(stride >= size);

    // setup the object metadata
    alloc->object_size = size;
    alloc->objet_align = align;
    alloc->object_stride = stride;

    // how many objects fit
    size_t header = ALIGN_UP(sizeof(slab_t), align);
    ASSERT(header < PAGE_SIZE);

    // calculate how many objects fit into a single slab
    size_t usable = PAGE_SIZE - header;
    size_t n = usable / stride;
    ASSERT(n != 0);
    ASSERT(n <= UINT16_MAX);
    alloc->objects_per_slab = n;

    list_init(&alloc->partial);
    list_init(&alloc->empty);
    list_init(&alloc->full);

    list_add(&m_allocators, &alloc->link);
}

void* mem_alloc(mem_alloc_t* alloc) {

    bool irq_state = irq_spinlock_acquire(&alloc->lock);

    // choose a slab to use, prefer partial slabs
    slab_t* slab = NULL;
    if (!list_is_empty(&alloc->partial)) {
        slab = list_first_entry(&alloc->partial, slab_t, link);

    } else if (!list_is_empty(&alloc->empty)) {
        slab = list_first_entry(&alloc->empty, slab_t, link);

        // move to partial list
        list_del(&slab->link);
        list_add(&alloc->partial, &slab->link);

    } else {
        // create new slab
        slab = slab_create(alloc);
        if (slab == NULL) {
            irq_spinlock_release(&alloc->lock, irq_state);
            return NULL;
        }
        list_add(&alloc->partial, &slab->link);
    }

    free_node_t* node = slab->free;
    ASSERT(node != NULL);

    slab->free = node->next;
    slab->in_use++;

    // if slab becomes full, move to the full list
    if (slab->in_use == slab->total) {
        list_del(&slab->link);
        list_add(&alloc->full, &slab->link);
    }

    irq_spinlock_release(&alloc->lock, irq_state);

    return node;
}

void mem_free(mem_alloc_t* alloc, void* p) {
    if (p == NULL) {
        return;
    }

    bool irq_state = irq_spinlock_acquire(&alloc->lock);

    // get the slab, and ensure it matches
    slab_t* slab = object_to_slab(p);
    ASSERT(slab->alloc == alloc);

    // add to the freelist of the slab
    free_node_t* node = p;
    node->next = slab->free;
    slab->free = node;

    // decrease the use count
    ASSERT(slab->in_use != 0);
    bool was_full = (slab->in_use == slab->total);
    slab->in_use--;

    // if it was full, move to partial
    if (was_full) {
        list_del(&slab->link);
        list_add(&alloc->partial, &slab->link);
    }

    // if now empty, move to empty
    if (slab->in_use == 0) {
        list_del(&slab->link);
        list_add(&alloc->empty, &slab->link);
    }

    irq_spinlock_release(&alloc->lock, irq_state);
}

void* mem_calloc(mem_alloc_t* alloc) {
    void* ptr = mem_alloc(alloc);
    if (ptr != NULL) {
        memset(ptr, 0, alloc->object_size);
    }
    return ptr;
}
