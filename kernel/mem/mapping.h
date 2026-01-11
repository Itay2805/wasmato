#pragma once

#include "lib/rbtree/rbtree.h"
#include "object/object.h"

/**
 * Represents a single vmar region
 */
typedef struct mapping {
    /**
     * The node in the VMAR that this region is part of
     */
    struct rb_node node;

    /**
     * The object that this region represents
     */
    object_t* object;

    /**
     * The address range that the region takes
     */
    void* start;

    /**
     * The page offset from the object that is mapped
     * - for VMAR is always zero
     * - for VMO is the page inside of it
     */
    size_t page_offset;

    /**
     * How many pages are mapped
     */
    size_t page_count;

    /**
     * Is this region writable
     */
    bool write;

    /**
     * Is this region executable
     */
    bool exec;

    /**
     * Is this a dynamic allocation of the mapping struct
     */
    bool dynamic;
} mapping_t;

