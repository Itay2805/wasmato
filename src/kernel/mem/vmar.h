#pragma once
#include <stdint.h>

#include "lib/string.h"
#include "lib/rbtree/rbtree_types.h"
#include "uapi/page.h"
#include "uapi/mapping.h"

/**
 * Virtual Memory Address Region
 */
typedef enum vmar_type : uint8_t {
    /**
     * A region, can contain more VMARs under it
     */
    VMAR_TYPE_REGION,

    /**
     * Contains allocated pages, need to be freed on release
     */
    VMAR_TYPE_ALLOC,

    /**
     * Contains static physical pages, should not be freed
     */
    VMAR_TYPE_PHYS,

    /**
     * Contains allocated stack pages
     */
    VMAR_TYPE_STACK,

    /**
     * Contains special pages, can't be freed, semantics
     * change per object
     */
    VMAR_TYPE_SPECIAL,
} vmar_type_t;

typedef struct vmar {
    /**
     * The node inside the parent region
     */
    rb_node_t node;

    /**
     * Name to help debug
     */
    const char* name;

    /**
     * The base address of the region
     */
    void* base;

    /**
     * The page count of the region
     */
    size_t page_count;

    /**
     * The type of the region
     */
    vmar_type_t type;

    /**
     * Is the vmar pinned, meaning it can't
     * be unmapped anymore
     */
    bool pinned;

    /**
     * Is the vmar locked, meaning no modifications
     * to it can be made:
     * - region: can't map more objects
     * - alloc: can't change protections
     */
    bool locked;

    union {
        struct {
            /**
             * The root into a tree of regions
             */
            rb_root_t root;
        } region;

        struct {
            /**
             * The protection used for the mapping
             */
            mapping_protection_t protection;
        } alloc;

        struct {
            /**
             * The physical address that this VMAR maps
             */
            uintptr_t phys;
        } phys;
    };

} vmar_t;

static inline void* vmar_end(const vmar_t* vmar) {
    size_t size = PAGES_TO_SIZE(vmar->page_count) - 1;
    return vmar->base + size;
}

/**
 * Initialize the VMAR object cache
 */
void init_vmar_alloc(void);

/**
 * Take the VMAR lock
 */
void vmar_lock(void);

/**
 * Unlock the VMAR lock
 */
void vmar_unlock(void);

/**
 * Reserve space for the child inside the parent, if the child base
 * is NULL one will be chosen
 *
 * Lock must be taken before entering the function
 *
 * @param parent    [IN] The parent to link
 * @param child     [IN] The child to link
 * @return true if success, false if no space
 */
bool vmar_reserve_static(vmar_t* parent, vmar_t* child);

/**
 * Reserve a virtual memory region, returning the vmar that represents it
 * the VMAR lock is going to be taken at the return of the function, and
 * you need to unlock it
 *
 * Lock must be taken before entering the function
 *
 * @param parent        [IN] The parent vmar to map inside of
 * @param page_count    [IN] The amount of pages to reserve
 * @param addr          [IN] Address to reserve, NULL for any address
 * @return NULL if out of memory or not space, the vmar otherwise
 */
vmar_t* vmar_reserve(vmar_t* parent, size_t page_count, void* addr);

/**
 * Similar to reserve but maps virtual memory
 *
 * Lock must be taken before entering the function
 *
 * @param parent        [IN] The parent region
 * @param page_count    [IN] The amount of pages to allocate
 * @param addr          [IN] Address to reserve, NULL for any address
 * @param pin           [IN] Should the region be pinned
 * @return NULL if out of memory or not space, the vmar otherwise
 */
vmar_t* vmar_allocate(vmar_t* parent, size_t page_count, void* addr);

/**
 * Change the protection of the given region, must be an allocated region
 * that is not locked.
*
 * Lock must be taken before entering the function
 *
 * @param mapping       [IN]
 * @param protection    [IN]
 */
void vmar_protect(void* mapping, mapping_protection_t protection);

/**
 * Find an entry inside the region
 *
 * @param parent
 * @param addr
 * @return
 */
vmar_t* vmar_find(vmar_t* parent, void* addr);

/**
 * Search for a mapping inside of the vmar
 *
 * Lock must be taken before entering the function
 *
 * @param root          [IN]
 * @param addr          [IN]
 * @return The VMAR of the mapping or NULL if not found
 */
vmar_t* vmar_find_mapping(vmar_t* root, void* addr);

/**
 * Free the VMAR region at the address
 *
 * Lock must be taken before entering the function
 *
 * @param base          [IN] Free
 */
void vmar_free(vmar_t* vmar);

/**
 * Dump the given vmar, lock must be taken
 *
 * Lock must be taken before entering the function
 */
void vmar_dump(vmar_t* vmar);
