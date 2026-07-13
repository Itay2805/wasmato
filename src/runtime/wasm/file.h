#pragma once

#include "lib/defs.h"
#include <stdatomic.h>
#include <stdint.h>

typedef struct file_ops file_ops_t;

typedef enum file_signals : uint32_t {
    FILE_SIGNAL_READ_READY = BIT0,
    FILE_SIGNAL_WRITE_READY = BIT1,
    FILE_SIGNAL_CLOSED = BIT2,
} file_signals_t;

typedef struct file {
    /** 
     * The signals of the file, also used for the atomic wait on it
     */
    _Atomic(file_signals_t) signals;

    /** 
     * The file operations of this file 
     */
    const file_ops_t* ops;

    /**
     * The amount of uses on this file right now
     * 
     * NOTE: a non-zero ref count doesn't mean it is 
     *       actually attached to an FD
     */
    atomic_size_t ref_count;
} file_t;

typedef struct file_ops {
    /**
     * called when the file is closed (not when the ref-count 
     * is zero), this should notify any ref holder that it should 
     * clean itself up
     */
    void (*close)(file_t* file);

    /**
     * called when the file is freed
     */
    void (*free)(file_t* file);
} file_ops_t;

file_t* file_get(file_t* file);
void file_put(file_t* file);

/** 
 * A wrapper for a kernel handle as a file descriptor
 */
typedef struct irq_file {
    file_t file;

    /** 
     * The handle to use
     */
    uint64_t handle;
} irq_file_t;
