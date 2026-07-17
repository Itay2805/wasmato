#pragma once

#include "lib/defs.h"
#include "lib/except.h"
#include "sync/mutex.h"
#include "uapi/wait.h"
#include <stdatomic.h>
#include <stdint.h>

typedef enum object_type : uint8_t {
    /** 
     * This is a WASI file
     */
    OBJECT_TYPE_WASI_FILE,

    /** 
     * This is a backing for a kernel object
     */
    OBJECT_TYPE_INTERRUPT,

    /**
     * This is a channel endpoint
     */
    OBJECT_TYPE_CHANNEL,
} object_type_t;

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

    /**
     * The object is readable, the exact meaning depends on the object type
     */
    SIGNAL_READABLE = BIT2,

    /**
     * The object is writable, the exact meaning depends on the object type
     */
    SIGNAL_WRITABLE = BIT3,
} signals_t;

typedef struct object {
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
     * the signals of the object
     */
    _Atomic(uint32_t) signals; 

    /** 
     * Called after the file was closed, meaning it is marked 
     * as closed and marked for the peer as closed
     *
     * NOTE: at this point there might still be pointers to the 
     *       object that are in the middle of an operation
     */
    void (*close)(struct object* obj);

    /** 
     * Called when the object is about to be freed 
     */
    void (*free)(struct object* obj);

    /**
     * The object type
     */
    object_type_t type;
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
 * Assert signals on the given object
 */
void object_signal(object_t* object, uint32_t set_mask);

/**
 * De-assert signals on the given object
 */
void object_clear_signal(object_t* object, uint32_t clear_mask);

/**
 * Setup a kernel wait-entry for the given object, this should be used to construct an array 
 * that can be given to sys_atomic_wait directly.
 *
 * returns the pending signals if already signaled, zero otherwise
 */
uint32_t object_prepare_wait(object_t* object, uint32_t signals, wait_entry_t* entry);

/** 
 * Wait on a single object for the given signals, this is a 
 * shorthand to make it easier to write sync code.
 */
err_t object_wait_one(object_t* object, uint32_t signals, uint64_t deadline, uint32_t* observed);
