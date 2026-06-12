#include "wasi.h"

#include "lib/defs.h"
#include "lib/list.h"
#include "lib/log.h"
#include "lib/string.h"
#include "lib/syscall.h"
#include "wasm/errno.h"
#include "wasm/proc.h"
#include <stdint.h>

typedef enum wasi_clockid : uint32_t {
    WASI_CLOCKID_REALTIME = 0,
    WASI_CLOCKID_MONOTONIC = 1,
    WASI_CLOCKID_PROCESS_CPUTIME_ID = 2,
    WASI_CLOCKID_THREAD_CPUTIME_ID = 3,
} wasi_clockid_t;

typedef enum wasi_whence : uint8_t {
    WASI_WHENCE_SET = 0,
    WASI_WHENCE_CUR = 1,
    WASI_WHENCE_END = 2,
} wasi_whence_t;

typedef uint64_t wasi_timestamp_t;

typedef uint32_t wasi_exitcode_t;

typedef int wasi_fd_t;

typedef uint64_t wasi_filesize_t;
typedef int64_t wasi_filedelta_t;

typedef uint32_t wasm_ptr_t;
typedef uint32_t wasi_size_t;


////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FD handling
////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct wasi_ciovec {
    wasm_ptr_t buf;
    wasi_size_t buf_len;
} wasi_ciovec_t;

typedef enum wasi_filetype : uint8_t {
    WASI_FILETYPE_UNKNOWN = 0,
    WASI_FILETYPE_BLOCK_DEVICE = 1,
    WASI_FILETYPE_CHARACTER_DEVICE = 2,
    WASI_FILETYPE_DIRECTORY = 3,
    WASI_FILETYPE_REGULAR_FILE = 4,
    WASI_FILETYPE_SOCKET_DGRAM = 5,
    WASI_FILETYPE_SOCKET_STREAM = 6,
    WASI_FILETYPE_SYMBOLIC_LINK = 7,
} wasi_filetype_t;

typedef enum wasi_fdflags : uint16_t {
    WASI_FDFLAGS_APPEND = 1 << 0,
    WASI_FDFLAGS_DSYNC = 1 << 1,
    WASI_FDFLAGS_NONBLOCK = 1 << 2,
    WASI_FDFLAGS_RSYNC = 1 << 3,
    WASI_FDFLAGS_SYNC = 1 << 4,
} wasi_fdflags_t;

typedef enum wasi_rights : uint64_t {
    WASI_RIGHTS_FD_DATASYNC  = 1 << 0,
    WASI_RIGHTS_FD_READ  = 1 << 1,
    WASI_RIGHTS_FD_SEEK  = 1 << 2,
    WASI_RIGHTS_FD_FDSTAT_SET_FLAGS  = 1 << 3,
    WASI_RIGHTS_FD_SYNC  = 1 << 4,
    WASI_RIGHTS_FD_TELL  = 1 << 5,
    WASI_RIGHTS_FD_WRITE  = 1 << 6,
    WASI_RIGHTS_FD_ADVISE  = 1 << 7,
    WASI_RIGHTS_FD_ALLOCATE  = 1 << 8,
    WASI_RIGHTS_PATH_CREATE_DIRECTORY  = 1 << 9,
    WASI_RIGHTS_PATH_CREATE_FILE  = 1 << 10,
    WASI_RIGHTS_PATH_LINK_SOURCE  = 1 << 11,
    WASI_RIGHTS_PATH_LINK_TARGET  = 1 << 12,
    WASI_RIGHTS_PATH_OPEN  = 1 << 13,
    WASI_RIGHTS_FD_READDIR  = 1 << 14,
    WASI_RIGHTS_PATH_READLINK  = 1 << 15,
    WASI_RIGHTS_PATH_RENAME_SOURCE  = 1 << 16,
    WASI_RIGHTS_PATH_RENAME_TARGET  = 1 << 17,
    WASI_RIGHTS_PATH_FILESTAT_GET  = 1 << 18,
    WASI_RIGHTS_PATH_FILESTAT_SET_SIZE  = 1 << 19,
    WASI_RIGHTS_PATH_FILESTAT_SET_TIMES  = 1 << 20,
    WASI_RIGHTS_FD_FILESTAT_GET  = 1 << 21,
    WASI_RIGHTS_FD_FILESTAT_SET_SIZE  = 1 << 22,
    WASI_RIGHTS_FD_FILESTAT_SET_TIMES  = 1 << 23,
    WASI_RIGHTS_PATH_SYMLINK  = 1 << 24,
    WASI_RIGHTS_PATH_REMOVE_DIRECTORY  = 1 << 25,
    WASI_RIGHTS_PATH_UNLINK_FILE  = 1 << 26,
    WASI_RIGHTS_POLL_FD_READWRITE  = 1 << 27,
    WASI_RIGHTS_SOCK_SHUTDOWN  = 1 << 28,
    WASI_RIGHTS_SOCK_ACCEPT  = 1 << 29,
} wasi_rights_t;

typedef struct wasi_fdstat_t {
    wasi_filetype_t fs_filetype;
    wasi_fdflags_t fs_flags;
    wasi_rights_t fs_rights_base;
    wasi_rights_t fs_rights_inheriting;
} wasi_fdstat_t;

static wasi_errno_t wasi_fd_write(
    void* memory_base, void* state_base, 
    wasi_fd_t fd, 
    wasm_ptr_t _iovs, wasi_size_t iovs_len, 
    wasm_ptr_t _retptr0
) {
    wasi_ciovec_t* iovs = memory_base + _iovs;
    wasi_size_t* retptr0 = memory_base + _retptr0;

    if (fd != 1 && fd != 2) {
        return WASI_ERRNO_BADF;
    }

    for (size_t i = 0; i < iovs_len; i++) {
        void* buf = memory_base + iovs[i].buf;
        sys_debug_print(buf, iovs[i].buf_len);
        *retptr0 += iovs[i].buf_len;
    }

    return WASI_ERRNO_SUCCESS;
}

static wasi_errno_t wasi_fd_seek(
    void* memory_base, void* state_base, 
    wasi_fd_t fd, wasi_filedelta_t offset, wasi_whence_t whence, 
    wasm_ptr_t _retptr0
) {
    ERROR("TODO: wasi_fd_seek");
    return WASI_ERRNO_NOTSUP;
}

static wasi_errno_t wasi_fd_fdstat_get(
    void* memory_base, void* state_base, 
    wasi_fd_t fd, 
    wasm_ptr_t _retptr0
) {
    // JUST A STUB
    wasi_fdstat_t* retptr0 = memory_base + _retptr0;
    if (fd != 1 && fd != 2) {
        return WASI_ERRNO_BADF;
    }
    retptr0->fs_filetype = WASI_FILETYPE_CHARACTER_DEVICE;
    retptr0->fs_rights_base |= WASI_RIGHTS_FD_WRITE | WASI_RIGHTS_FD_READ;
    return WASI_ERRNO_SUCCESS;
}

static wasi_errno_t wasi_poll_oneoff(
    void* memory_base, void* state_base, 
    wasm_ptr_t _in, wasm_ptr_t _out, uint32_t nsubscriptions, 
    wasm_ptr_t _retptr0
) {
    ERROR("TODO: wasi_poll_oneoff");
    return WASI_ERRNO_NOTSUP;
}

static wasi_errno_t wasi_fd_close(void* memory_base, void* state_base, wasi_fd_t fd) {
    ERROR("TODO: wasi_fd_close");
    return WASI_ERRNO_NOTSUP;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Time handling
////////////////////////////////////////////////////////////////////////////////////////////////////////////

static wasi_errno_t wasi_clock_time_get(
    void* memory_base, void* state_base, 
    wasi_clockid_t id, wasi_timestamp_t precision, 
    wasm_ptr_t _retptr0
) {
    ERROR("TODO: wasi_clock_time_get");
    return WASI_ERRNO_NOTSUP;    
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Process and thread handling
////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void wasi_proc_exit(void* memory_base, void* state_base, wasi_exitcode_t rval) {
    wasm_proc_t* proc = wasm_current_proc(state_base);

    // TODO: remove this
    TRACE("proc_exit: process exited with 0x%x", rval);

    // and we can exit
    wasm_thread_exit(state_base);
}

static void wasi_sched_yield(void* memory_base, void* state_base) {
    sys_thread_yield();
}

typedef struct wasi_export {
    const char* name;
    void* func;
} wasi_export_t;

static const wasi_export_t m_wasi_exports[] = {
    { "fd_write", wasi_fd_write },
    { "fd_seek", wasi_fd_seek },
    { "fd_fdstat_get", wasi_fd_fdstat_get },
    { "poll_oneoff", wasi_poll_oneoff },
    { "fd_close", wasi_fd_close },

    { "clock_time_get", wasi_clock_time_get },

    { "proc_exit", wasi_proc_exit },
    { "sched_yield", wasi_sched_yield },
};

void* wasi_resolve_import(const char* name) {
    for (int i = 0; i < ARRAY_LENGTH(m_wasi_exports); i++) {
        if (strcmp(m_wasi_exports[i].name, name) == 0) {
            return m_wasi_exports[i].func;
        }
    }
    return nullptr;
}
