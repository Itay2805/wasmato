#include "region.h"

#include "arch/paging.h"
#include "kernel/alloc.h"
#include "lib/string.h"
#include "lib/rbtree/rbtree.h"
#include "sync/spinlock.h"

typedef struct range {
    void* start;
    uintptr_t page_count;
} range_t;

/**
 * A lock to protect all region operations
 * TODO: move to fine-grained locks
 */
static irq_spinlock_t m_region_lock = IRQ_SPINLOCK_INIT;

static void rbtree_find_insert_spot(
    struct rb_root* root, void* base,
    struct rb_node** out_parent,
    struct rb_node** *out_link,
    region_t** out_prev,
    region_t** out_next
) {
    struct rb_node **link = &root->rb_node;
    struct rb_node *parent = NULL;
    region_t* prev = NULL;
    region_t* next = NULL;

    while (*link) {
        region_t* cur = rb_entry(*link, region_t, node);

        parent = *link;
        if (base < cur->base) {
            next = cur;
            link = &(*link)->rb_left;
        } else {
            prev = cur;
            link = &(*link)->rb_right;
        }
    }

    *out_parent = parent;
    *out_link = link;
    if (out_prev) *out_prev = prev;
    if (out_next) *out_next = next;
}

static inline bool ranges_overlap(void* a0, void* a1, void* b0, void* b1) {
    return (a0 < b1) && (b0 < a1);
}

static void* rbtree_try_gap(
    struct rb_root* root,
    size_t size, size_t align,
    void* arena_base, void* arena_end,
    void* gap_lo, void* gap_hi,
    struct rb_node** out_parent,
    struct rb_node*** out_link
) {
    if (gap_lo < arena_base) gap_lo = arena_base;
    if (gap_hi > arena_end)  gap_hi = arena_end;
    if (gap_hi > gap_lo && (gap_hi - gap_lo) >= size) {
        void* max_base = ALIGN_DOWN(gap_hi - size, align);
        void* min_base = ALIGN_UP(gap_lo, align);
        if (max_base >= min_base) {
            void* base = max_base;
            rbtree_find_insert_spot(root, base, out_parent, out_link, nullptr, nullptr);
            return base;
        }
    }
    return nullptr;
}

static void* rbtree_find_top_find_space(
    region_t* root,
	size_t size, size_t align,
	struct rb_node** out_parent, struct rb_node*** out_link
) {
	if (size > PAGES_TO_SIZE(root->page_count))
		return NULL;

    void* arena_base = root->base;
    void* arena_end = region_end(root);

	void* hi = arena_end;

	struct rb_node* n = rb_last(&root->root);
	while (n) {
		region_t* cur = rb_entry(n, region_t, node);

		// gap above cur is [cur->end, hi)
		void* base = rbtree_try_gap(
		    &root->root,
		    size, align,
		    arena_base, arena_end,
		    region_end(cur), hi,
		    out_parent, out_link
		);
	    if (base != NULL) {
	        return base;
	    }

		// next candidates must be below cur->base
		ASSERT(cur->base < hi);
		hi = cur->base;

		n = rb_prev(n);
	}

	// finally the bottom gap
	return rbtree_try_gap(
	    &root->root,
        size, align,
        arena_base, arena_end,
	    arena_base, hi,
		out_parent, out_link
	);
}

static bool rbtree_find_exact(
    struct rb_root *root, void* base, void* end,
    struct rb_node **out_parent,
    struct rb_node ***out_link
) {
    if (base >= end) return false;

    region_t* prev = NULL;
    region_t* next = NULL;
    struct rb_node* parent = NULL;
    struct rb_node** link = NULL;
    rbtree_find_insert_spot(root, base, &parent, &link, &prev, &next);

    if (prev && ranges_overlap(prev->base, region_end(prev), base, end))
        return false;
    if (next && ranges_overlap(next->base, region_end(next), base, end))
        return false;

    *out_parent = parent;
    *out_link = link;
    return true;
}

bool region_reserve(region_t* parent_region, region_t* child_region, size_t order) {
    size_t alignment = 1 << (order + PAGE_SHIFT);

    // validate child
    ASSERT(child_region->page_count != 0);

    // validate parent
    ASSERT(parent_region->type == REGION_TYPE_DEFAULT);
    ASSERT(!parent_region->locked);

    bool irq_state = irq_spinlock_acquire(&m_region_lock);

    // these two code paths will return us the location that we need
    // to place the region into
    struct rb_node* parent = NULL;
    struct rb_node** link = NULL;
    if (child_region->base == NULL) {
        // no base address given, allocate one
        void* new_base = rbtree_find_top_find_space(
            parent_region,
            PAGES_TO_SIZE(child_region->page_count), alignment,
            &parent, &link
        );
        if (new_base == NULL) {
            irq_spinlock_release(&m_region_lock, irq_state);
            return false;
        }
        child_region->base = new_base;
    } else {
        // base address given, ensure it doesn't overlap
        // we consider it an error if it overlaps because
        // no user path will ever pass an exact address
        ASSERT(rbtree_find_exact(
            &parent_region->root,
            child_region->base, region_end(child_region),
            &parent, &link
        ));
    }

    // and now we can actually insert it
    rb_link_node(&child_region->node, parent, link);
    rb_insert_color(&child_region->node, &parent_region->root);

    irq_spinlock_release(&m_region_lock, irq_state);

    return true;
}

static mem_alloc_t m_region_alloc;

void init_region_alloc(void) {
    mem_alloc_init(&m_region_alloc, sizeof(region_t), alignof(region_t));
}

region_t* region_allocate(region_t* region, size_t page_count, size_t order, void* addr) {
    region_t* child = mem_calloc(&m_region_alloc);
    if (child == NULL) {
        return NULL;
    }

    child->base = addr;
    child->page_count = page_count;

    child->type = REGION_TYPE_MAPPING_ALLOC;
    child->cache_policy = MAPPING_CACHE_POLICY_CACHED;
    child->protection = MAPPING_PROTECTION_RW;

    // actually reserve it, if failed free the region and
    // return null
    if (!region_reserve(region, child, order)) {
        mem_free(&m_region_alloc, child);
        return NULL;
    }

    return child;
}

region_t* region_map_phys(region_t* region, uint64_t phys, mapping_cache_policy_t cache, size_t page_count, void* addr) {
    region_t* child = mem_calloc(&m_region_alloc);
    if (child == NULL) {
        return NULL;
    }

    region->base = addr;
    region->page_count = page_count;

    region->phys = phys;
    region->type = REGION_TYPE_MAPPING_PHYS;
    region->cache_policy = cache;
    region->protection = MAPPING_PROTECTION_RW;

    // actually reserve it, if failed free the region and
    // return null
    if (!region_reserve(region, child, 0)) {
        mem_free(&m_region_alloc, child);
        return NULL;
    }

    return region;
}

void mapping_protect(region_t* region, void* addr, mapping_protection_t protection) {
    // TODO: this
}

void mapping_unmap(region_t* region, void* addr) {
    // TODO: this
}

void region_free(region_t* region) {
    // TODO: this
}

static region_t* region_find_mapping_locked(region_t* region, void* addr) {
    // not inside of this region at all
    if (addr < region->base || region_end(region) < addr) {
        return NULL;
    }

    region_t* found = NULL;
    struct rb_node* node = region->root.rb_node;
    while (node) {
        region_t* child = rb_entry(node, region_t, node);

        // if overlaps we found it
        if (child->base <= addr && addr < region_end(child)) {
            found = child;
            break;
        }

        if (addr < child->base) {
            node = node->rb_left;
        } else {
            node = node->rb_right;
        }
    }

    // not found, return null
    if (found == NULL) {
        return NULL;
    }

    if (found->type == REGION_TYPE_DEFAULT) {
        // this is another region, search it, use tail call for fun and profit
        [[clang::musttail]] return region_find_mapping_locked(found, addr);
    }

    // this is an actual mapping, return it
    return found;
}

region_t* region_find_mapping(region_t* region, void* addr) {
    bool irq_state = irq_spinlock_acquire(&m_region_lock);
    region_t* found = region_find_mapping_locked(region, addr);
    irq_spinlock_release(&m_region_lock, irq_state);
    return found;
}

static void region_print_tree_rec(region_t* region, char* prefix, size_t plen, bool is_last) {
    if (plen) {
        debug_print("%s", prefix);
        debug_print("%s", is_last ? "└── " : "├── ");
    }

    const char* name = region->name;
    if (name == NULL) {
        name = "<anonymous>";
    }

    const char* type_str = NULL;
    if (region->type == REGION_TYPE_MAPPING_PHYS) {
        type_str = "PHYS   ";
    } else if (region->type == REGION_TYPE_MAPPING_ALLOC) {
        type_str = "ALLOC  ";
    } else if (region->type == REGION_TYPE_MAPPING_SPECIAL) {
        type_str = "SPECIAL";
    } else if (region->type == REGION_TYPE_DEFAULT) {
        type_str = "REGION ";
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

    if (region->type == REGION_TYPE_DEFAULT) {
        // this is a vmar that might have children
        if (rb_first(&region->root) == NULL) {
            // no children, exit
            return;
        }

        const char *ext = is_last ? "    " : "│   ";
        memcpy(prefix + plen, ext, 4);
        size_t new_plen = plen + 4;
        prefix[new_plen] = '\0';

        for (struct rb_node* n = rb_first(&region->root); n != NULL; n = rb_next(n)) {
            region_t* mapping = containerof(n, region_t, node);
            region_print_tree_rec(mapping, prefix, new_plen, rb_next(n) == NULL);
        }

        prefix[plen] = '\0';
    }
}

void region_dump(region_t* region) {
    bool irq_state = irq_spinlock_acquire(&m_region_lock);
    char prefix[256] = {0};
    region_print_tree_rec(region, prefix, 0, true);
    irq_spinlock_release(&m_region_lock, irq_state);
}
