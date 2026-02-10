#include "vmar.h"

#include "mappings.h"
#include "internal/virt.h"
#include "kernel/alloc.h"
#include "lib/assert.h"
#include "lib/rbtree/rbtree.h"

static mem_alloc_t m_vmar_alloc;

static irq_spinlock_t m_vmar_lock = IRQ_SPINLOCK_INIT;
static bool m_vmar_lock_irq_state = false;

void init_vmar_alloc(void) {
    mem_alloc_init(&m_vmar_alloc, sizeof(vmar_t), alignof(vmar_t));
}

void vmar_lock(void) {
    m_vmar_lock_irq_state = irq_spinlock_acquire(&m_vmar_lock);
}

void vmar_unlock(void) {
    irq_spinlock_release(&m_vmar_lock, m_vmar_lock_irq_state);
}

//----------------------------------------------------------------------------------------------------------------------
// Searching
//----------------------------------------------------------------------------------------------------------------------

static inline bool ranges_overlap(void* a0, void* a1, void* b0, void* b1) {
    return (a0 < b1) && (b0 < a1);
}

static int __always_inline vmar_cmp(const void* key, const struct rb_node* node) {
    const vmar_t* node_entry = rb_entry(node, vmar_t, node);

    // check if inside
    if (node_entry->base <= key && key <= vmar_end(node_entry))
        return 0;

    if (key < node_entry->base)
        return -1;
    else
        return 1;
}

vmar_t* vmar_find(vmar_t* parent, void* addr) {
    ASSERT(parent->type == VMAR_TYPE_REGION);
    return rb_entry_safe(rb_find(addr, &parent->region.root, vmar_cmp), vmar_t, node);
}

static int __always_inline vmar_cmp_overlap(const void* key, const struct rb_node* node) {
    const vmar_t* key_entry = key;
    const vmar_t* node_entry = rb_entry(node, vmar_t, node);

    // if overlaps its a match
    if (ranges_overlap(
        key_entry->base, vmar_end(key_entry),
        node_entry->base, vmar_end(node_entry)
    )) {
        return 0;
    }

    // otherwise go off the base
    if (key_entry->base < node_entry->base)
        return -1;
    else
        return 1;
}

static vmar_t* vmar_find_overlapping(vmar_t* parent, vmar_t* child) {
    return rb_entry_safe(rb_find(child, &parent->region.root, vmar_cmp_overlap), vmar_t, node);
}

vmar_t* vmar_find_mapping(vmar_t* entry, void* addr) {
    ASSERT(entry->type == VMAR_TYPE_REGION);
    for (;;) {
        // search for an exact match
        entry = vmar_find(entry, addr);
        if (entry == nullptr) {
            return nullptr;
        }

        // if its a region continue searching
        if (entry->type != VMAR_TYPE_REGION) {
            return entry;
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------
// Range allocation
//----------------------------------------------------------------------------------------------------------------------

static void* vmar_find_gap(vmar_t* parent, size_t size) {
    void* prev_node_start = vmar_end(parent);
    for (rb_node_t* node = rb_last(&parent->region.root); node != nullptr; node = rb_prev(node)) {
        vmar_t* entry = rb_entry(node, vmar_t, node);

        // get the gap
        void* gap_start = vmar_end(entry);
        void* gap_end = prev_node_start;
        prev_node_start = entry->base - 1;

        // if gap has enough space then return from the end
        if (gap_end - gap_start >= size) {
            return gap_end - size + 1;
        }
    }

    // check the area between the first node and the start of the entire region
    void* gap_start = parent->base;
    void* gap_end = prev_node_start;
    if (gap_end - gap_start >= size) {
        return gap_end - size + 1;
    }

    // not found
    return nullptr;
}

static __always_inline bool vmar_less(struct rb_node* a, const struct rb_node* b) {
    const vmar_t* vmar_a = rb_entry(a, vmar_t, node);
    const vmar_t* vmar_b = rb_entry(b, vmar_t, node);
    return vmar_a->base < vmar_b->base;
}

bool vmar_reserve_static(vmar_t* parent, vmar_t* child) {
    ASSERT(child->page_count != 0);
    ASSERT(parent->type == VMAR_TYPE_REGION);
    ASSERT(!parent->locked);

    // start by either allocating or
    // verifying the given address
    if (child->base == nullptr) {
        // search for an empty region
        void* child_base = vmar_find_gap(parent, PAGES_TO_SIZE(child->page_count));
        if (child_base == nullptr) {
            return false;
        }
        child->base = child_base;
    } else {
        // ensure child within bounds of parent, and that
        // the address is page aligned
        ASSERT(parent->base <= child->base);
        ASSERT(vmar_end(child) <= vmar_end(parent));
        ASSERT(((uintptr_t)child->base % PAGE_SIZE) == 0);
        ASSERT(vmar_find_overlapping(parent, child) == nullptr);
    }

    // we have a good address, link it
    // TODO: maybe we can somehow use the searches we do before
    //       to get the insert address right away
    rb_add(&child->node, &parent->region.root, vmar_less);

    return true;
}

//----------------------------------------------------------------------------------------------------------------------
// Low level APIs
//----------------------------------------------------------------------------------------------------------------------

vmar_t* vmar_reserve(vmar_t* parent, size_t page_count, void* addr) {
    // allocate a child object
    vmar_t* child = mem_calloc(&m_vmar_alloc);
    if (child == nullptr)
        return nullptr;

    // setup the child object
    child->type = VMAR_TYPE_REGION;
    child->base = addr;
    child->page_count = page_count;
    child->region.root = RB_ROOT;

    // reserve it
    if (!vmar_reserve_static(parent, child)) {
        mem_free(&m_vmar_alloc, child);
        return nullptr;
    }

    return child;
}

vmar_t* vmar_allocate(vmar_t* parent, size_t page_count, void* addr) {
    // allocate a child object
    vmar_t* child = mem_calloc(&m_vmar_alloc);
    if (child == nullptr)
        return nullptr;
\
    // setup the child object
    child->type = VMAR_TYPE_ALLOC;
    child->base = addr;
    child->page_count = page_count;
    child->alloc.protection = MAPPING_PROTECTION_RW;

    // reserve it
    if (!vmar_reserve_static(parent, child)) {
        mem_free(&m_vmar_alloc, child);
        return nullptr;
    }

    return child;
}

//----------------------------------------------------------------------------------------------------------------------
// High level APIs
//----------------------------------------------------------------------------------------------------------------------

void vmar_protect(void* mapping, mapping_protection_t protection) {
    vmar_t* vmar = vmar_find_mapping(&g_user_memory, mapping);
    ASSERT(vmar != nullptr);
    ASSERT(vmar->base == mapping);

    ASSERT(vmar->type == VMAR_TYPE_ALLOC);
    ASSERT(!vmar->locked);

    // change protections and lock
    vmar->alloc.protection = protection;
    vmar->locked = true;

    // tell the vmm to change protection of existing pages
    virt_protect(vmar->base, vmar->page_count, protection);
}

//----------------------------------------------------------------------------------------------------------------------
// Freeing
//----------------------------------------------------------------------------------------------------------------------

void vmar_free(vmar_t* vmar) {
    ASSERT(!"TODO: vmar_free");
}

//----------------------------------------------------------------------------------------------------------------------
// VMAR Debug
//----------------------------------------------------------------------------------------------------------------------

static void vmar_print_tree_rec(vmar_t* region, char* prefix, size_t plen, bool is_last) {
    if (plen) {
        debug_print("%s", prefix);
        debug_print("%s", is_last ? "└── " : "├── ");
    }

    const char* name = region->name;
    if (name == NULL) {
        name = "<anonymous>";
    }

    const char* type_str = NULL;
    if (region->type == VMAR_TYPE_PHYS) {
        type_str = "PHYS   ";
    } else if (region->type == VMAR_TYPE_ALLOC) {
        type_str = "ALLOC  ";
    } else if (region->type == VMAR_TYPE_SPECIAL) {
        type_str = "SPECIAL";
    } else if (region->type == VMAR_TYPE_REGION) {
        type_str = "REGION ";
    } else if (region->type == VMAR_TYPE_STACK) {
        type_str = "STACK  ";
    } else {
        ASSERT(false);
    }

    uintptr_t start = (uintptr_t)region->base;
    uintptr_t end = (uintptr_t)(region->base + (PAGES_TO_SIZE(region->page_count) - 1));
    debug_print("%s: 0x%08x'%08x-0x%08x'%08x: %ld pages [%s]\n",
        type_str,
        (uint32_t)(start >> 32), (uint32_t)start,
        (uint32_t)(end >> 32), (uint32_t)end,
        region->page_count,
        name
    );

    if (region->type == VMAR_TYPE_REGION) {
        // this is a vmar that might have children
        if (rb_first(&region->region.root) == NULL) {
            // no children, exit
            return;
        }

        const char *ext = is_last ? "    " : "│   ";
        memcpy(prefix + plen, ext, 4);
        size_t new_plen = plen + 4;
        prefix[new_plen] = '\0';

        for (struct rb_node* n = rb_first(&region->region.root); n != NULL; n = rb_next(n)) {
            vmar_t* mapping = containerof(n, vmar_t, node);
            vmar_print_tree_rec(mapping, prefix, new_plen, rb_next(n) == NULL);
        }

        prefix[plen] = '\0';
    }
}

void vmar_dump(vmar_t* vmar) {
    char prefix[256] = {0};
    vmar_print_tree_rec(vmar, prefix, 0, true);
}
