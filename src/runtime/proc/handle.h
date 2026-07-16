#pragma once

#include "sync/mutex.h"
#include "object.h"
#include <stdint.h>

typedef struct handle_table {
    /**
     * Lock to protect the handle table
     */
    mutex_t lock;

    /**
     * The capacity of the handle table
     */
    uint32_t capacity;

    /** 
     * A bitmap of all the objects that are in use, any bit 
     * which is zero is an entry available
     */
    uint64_t* open;

    /** 
     * The array of all the objects
     */
    object_t** array;
} handle_table_t;

/**
 * Allocate a new handle in the table, installing the object into it, this increases the handle
 * count of the object but not the ref-count, it is assumed whoever installs the table lends a ref
 * into the handle table.
 *
 * if we are out of memory returns a negative number, the handle number otherwise
 */
int handle_table_allocate(handle_table_t* table, object_t* object);

/** 
 * Install a handle on the given handle, if the handle was free returns true,
 * returns false otherwise
 */
bool handle_table_install(handle_table_t* table, object_t* object, int handle);

/** 
 * Lookup a handle from the fd table, increasing the ref-count, returns 
 * null if not found
 */
object_t* handle_table_lookup(handle_table_t* table, int handle);
