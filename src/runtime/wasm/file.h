#pragma once

#include "lib/defs.h"
#include "wasm/wasi.h"
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
     * The amount of procs this file is registered to, when it 
     * reaches zero we are going to close the file itself
     */
    atomic_size_t use_count;

    /**
     * The amount of people holding the struct, all use counts hold 
     * a single ref, and any time we get a file from the table we 
     * increase this ref, it is meant to keep the struct alive, not 
     * the file open
     */
    atomic_size_t ref_count;

    /**
     * The file stats of the file
     */
    wasi_fdstat_t fdstat;
} file_t;

static bool file_is_capable(file_t* file, wasi_rights_t rights) {
    return (file->fdstat.fs_rights_base & rights) == rights;
}

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

file_t* file_use_get(file_t* file);
void file_use_put(file_t* file);

