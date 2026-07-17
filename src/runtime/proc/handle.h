#pragma once

#include "lib/defs.h"
#include "sync/mutex.h"
#include "object.h"
#include <stdint.h>

typedef enum rights {
    /** 
     * The object can be waited on
     */
    RIGHT_WAIT = BIT0,

    /** 
     * The object can be read 
     */
    RIGHT_READ = BIT1,

    /** 
     * The object can be written to
     */
    RIGHT_WRITE = BIT2,
} rights_t;

typedef struct handle {
    /**
     * The object type
     */
    object_t* object;

    /** 
     * The rights of the fd
     */
    rights_t rights;
} handle_t;

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
    handle_t* array;
} handle_table_t;

/**
 * Allocate a new handle in the table, installing the object into it, this increases the handle
 * count of the object but not the ref-count, it is assumed whoever installs the table lends a ref
 * into the handle table.
 *
 * if we are out of memory returns a negative number, the handle number otherwise
 */
int handle_table_allocate(handle_table_t* table, object_t* object, rights_t rights);

/** 
 * Install a handle on the given handle, if the handle was free returns true,
 * returns false otherwise
 */
bool handle_table_install(handle_table_t* table, object_t* object, rights_t rights, int handle);

/** 
 * Lookup a handle from the fd table, increasing the ref-count, returns 
 * null if not found
 */
handle_t handle_table_lookup(handle_table_t* table, int handle);

/**
 * Close the given handle
 */
bool handle_table_close(handle_table_t* table, int handle);
