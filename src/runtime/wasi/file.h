#pragma once

#include "proc/object.h"
#include "wasi/wasip1.h"

typedef struct wasi_file {
    object_t object;

    /** 
     * The WASI fd stats
     */
    wasi_fdstat_t stats;
} wasi_file_t;

wasi_file_t* wasi_file_create(void);

wasi_file_t* wasi_file_from_object(object_t* object);

static inline bool wasi_file_is_capable(wasi_file_t* file, wasi_rights_t rights) {
    return (file->stats.fs_rights_base & rights) != 0;
}
