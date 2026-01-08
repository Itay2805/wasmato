#include "valloc.h"

#include "alloc.h"
#include "memory.h"
#include "phys.h"
#include "virt.h"
#include "lib/defs.h"
#include "lib/string.h"
#include "lib/rbtree/rbtree.h"
#include "sync/spinlock.h"

typedef union valloc_area {
    // when the entry is in use
    struct {
        struct rb_node node;
        void* start;
        void* end;
    };

    // when the entry is not in use
    list_entry_t freelist_entry;
} valloc_area_t;


static int valloc_area_cmp(const void* ptr, const struct rb_node* node) {
    valloc_area_t* area = containerof(node, valloc_area_t, node);
    if (area->start < ptr) {
        return -1;
    }

    if (area->start > ptr) {
        return 1;
    }

    return 0;
}

static bool valloc_area_less(struct rb_node* a, const struct rb_node* b) {
    valloc_area_t* aa = containerof(a, valloc_area_t, node);
    valloc_area_t* bb = containerof(b, valloc_area_t, node);
    return aa->start < bb->start;
}

/**
 * The bottom of the sbrk based allocator
 */
static void* m_valloc_bottom = HEAP_ADDR_START;

/**
 * The root of the virtual allocation tree,
 * for managing the mmap chunks, we use a cached
 * rbtree so we can find the bottom of the mmap
 * regions quickly
 */
static struct rb_root_cached m_valloc_root = RB_ROOT_CACHED;

/**
 * The lock to protect the entire thing
 */
static irq_spinlock_t m_valloc_lock = IRQ_SPINLOCK_INIT;

/**
 * The freelist of available descriptors, to avoid recursion we don't allocate
 * the area descriptor in the vmm but directly in the direct map
 */
static list_t m_valloc_area_freelist = LIST_INIT(&m_valloc_area_freelist);

static valloc_area_t* valloc_allocate_descriptor(void) {
    if (list_is_empty(&m_valloc_area_freelist)) {
        valloc_area_t* areas = phys_alloc(PAGE_SIZE);
        if (areas == NULL) {
            return NULL;
        }

        // add them to the free list
        size_t count = PAGE_SIZE / sizeof(valloc_area_t);
        for (size_t i = 0; i < count; i++) {
            list_add(&m_valloc_area_freelist, &areas[i].freelist_entry);
        }
    }

    // take the first entry and return it
    valloc_area_t* area = list_first_entry(&m_valloc_area_freelist, valloc_area_t, freelist_entry);
    list_del(&area->freelist_entry);
    return area;
}

static valloc_area_t* valloc_get_first_area(void) {
    struct rb_node* node = rb_first_cached(&m_valloc_root);
    if (node == NULL) {
        return NULL;
    }
    return containerof(node, valloc_area_t, node);
}

static void* valloc_get_mmap_bottom(void) {
    valloc_area_t* area = valloc_get_first_area();
    if (area == NULL) {
        return HEAP_ADDR_END;
    }
    return area->start;
}

void* valloc_expand(size_t size) {
    size = ALIGN_UP(size, PAGE_SIZE);

    bool irq_state = irq_spinlock_acquire(&m_valloc_lock);

    // move from the bottom, if we are out of space just fail
    void* alloc_bottom = m_valloc_bottom;
    if (alloc_bottom + size > valloc_get_mmap_bottom()) {
        irq_spinlock_release(&m_valloc_lock, irq_state);
        return NULL;
    }

    // allocate the pages themselves, if we are out of pages for this then
    // bail out as well
    if (IS_ERROR(virt_alloc(alloc_bottom, size / PAGE_SIZE))) {
        irq_spinlock_release(&m_valloc_lock, irq_state);
        return NULL;
    }

    m_valloc_bottom += size;

    irq_spinlock_release(&m_valloc_lock, irq_state);

    // return the new area
    return alloc_bottom + size;
}

static valloc_area_t* valloc_get_next_area(valloc_area_t* area) {
    struct rb_node* n = rb_next(&area->node);
    if (n == NULL) {
        return NULL;
    }
    return containerof(n, valloc_area_t, node);
}

static bool valloc_try_region(valloc_area_t* area, size_t size, void* start, void* end) {
    if (end - start >= size) {
        area->start = start;
        area->end = start + size;
        return true;
    }
    return false;
}

static bool valloc_first_fit(valloc_area_t* area, size_t size) {
    valloc_area_t* first = valloc_get_first_area();

    // iterate the tree to find an empty region
    valloc_area_t* next = NULL;
    for (valloc_area_t* cur = first; cur != NULL; cur = next) {
        next = valloc_get_next_area(cur);
        if (next == NULL) {
            // this is the last region, ensure that there is nothing
            // between the last region and the actual top of the heap
            if (valloc_try_region(area, size, cur->end, HEAP_ADDR_END)) {
                return true;
            }
        } else {
            // we have two entries, check in between them
            if (valloc_try_region(area, size, cur->end, next->start)) {
                return true;
            }
        }
    }

    // we could not find any region, take from the bottom of the heap
    void* base = first == NULL ? HEAP_ADDR_END : first->start;
    void* would_be_area = base - size;
    if (would_be_area < m_valloc_bottom) {
        // if the new area would be under the bottom of the
        // sbrk region then we are out of space
        return false;
    }

    area->start = would_be_area;
    area->end = would_be_area + size;
    return true;
}

void* valloc_alloc(size_t size) {
    size = ALIGN_UP(size, PAGE_SIZE);

    bool irq_state = irq_spinlock_acquire(&m_valloc_lock);

    // allocate a descriptor
    valloc_area_t* new = valloc_allocate_descriptor();
    if (new == NULL) {
        irq_spinlock_release(&m_valloc_lock, irq_state);
        return NULL;
    }

    // attempt to allocate a region from the rbtree
    if (!valloc_first_fit(new, size)) {
        // we failed to find a region, so the whole allocation has failed
        list_add(&m_valloc_area_freelist, &new->freelist_entry);
        irq_spinlock_release(&m_valloc_lock, irq_state);
        return NULL;
    }

    // allocate the region
    if (IS_ERROR(virt_alloc(new->start, size / PAGE_SIZE))) {
        list_add(&m_valloc_area_freelist, &new->freelist_entry);
        irq_spinlock_release(&m_valloc_lock, irq_state);
        return NULL;
    }

    // add node to the tree
    rb_add_cached(&new->node, &m_valloc_root, valloc_area_less);

    irq_spinlock_release(&m_valloc_lock, irq_state);

    return new->start;
}

void valloc_free(void* ptr, size_t size) {
    size = ALIGN_UP(size, PAGE_SIZE);
    bool irq_state = irq_spinlock_acquire(&m_valloc_lock);

    // find the node of this region
    struct rb_node* node = rb_find(ptr, &m_valloc_root.rb_root, valloc_area_cmp);
    ASSERT(node != NULL);
    valloc_area_t* area = containerof(node, valloc_area_t, node);

    // free the actual memory backing the region
    virt_free(area->start, (area->end - area->start) / PAGE_SIZE);

    // remove it from the tree
    rb_erase_cached(&area->node, &m_valloc_root);

    // add back to the freelist
    list_add(&m_valloc_area_freelist, &area->freelist_entry);

    irq_spinlock_release(&m_valloc_lock, irq_state);
}

