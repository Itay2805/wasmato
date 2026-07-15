#pragma once

#include <stdint.h>


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

void* wasi_resolve_import(const char* name);
