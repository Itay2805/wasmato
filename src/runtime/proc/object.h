#pragma once

#include "lib/defs.h"
#include "lib/except.h"
#include "sync/mutex.h"
#include "uapi/wait.h"
#include <stdatomic.h>
#include <stdint.h>

/**
 * These are signals with a common definition
 */
typedef enum signals : uint32_t {
    /**
     * All handles to the objects were closed, 
     * the object is no longer valid
     */
    SIGNAL_CLOSED = BIT0,

    /**
     * The peer of the object was closed, any operations 
     * related to it are no longer valid
     */
    SIGNAL_PEER_CLOSED = BIT1,
} signals_t;

typedef struct object {
    /**
     * the signals of the object
     */
    _Atomic(uint32_t) signals; 

    /**
     * The amount of open handles for this object, once it 
     * reaches zero we mark the object as closed
     */
    atomic_size_t handle_count;

    /**
     * The ref-count of the object, once this reaches zero the 
     * object should already be closed and we should free the 
     * structure
     */
    atomic_size_t ref_count;

    /**
     * The peer object, not every object has a peer
     */
    struct object* peer;
} object_t;

object_t* object_get(object_t* object);
void object_put(object_t* object);

object_t* object_handle_get(object_t* object);
void object_handle_put(object_t* object);

/** 
 * Initialize a new object
 */
void object_init(object_t* object);

/**
 * Assert and de-assert signals on the given object
 */
void object_signal(object_t* object, uint32_t clear_mask, uint32_t set_mask);

/**
 * Setup a kernel wait-entry for the given object, this should be used to construct an array 
 * that can be given to sys_atomic_wait directly.
 *
 * returns the pending signals if already signaled, zero otherwise
 */
uint32_t object_create_wait_entry(object_t* object, uint32_t signals, wait_entry_t* entry);

/** 
 * Wait on a single object for the given signals, this is a 
 * shorthand to make it easier to write sync code.
 */
err_t object_wait_one(object_t* object, uint32_t signals, uint64_t deadline, uint32_t* observed);
