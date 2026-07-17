#pragma once

#include "lib/defs.h"
#include <stddef.h>
#include <stdint.h>

typedef uint32_t wasi_ptr_t;

_Static_assert(_Alignof(int8_t) == 1, "non-wasi data layout");
_Static_assert(_Alignof(uint8_t) == 1, "non-wasi data layout");
_Static_assert(_Alignof(int16_t) == 2, "non-wasi data layout");
_Static_assert(_Alignof(uint16_t) == 2, "non-wasi data layout");
_Static_assert(_Alignof(int32_t) == 4, "non-wasi data layout");
_Static_assert(_Alignof(uint32_t) == 4, "non-wasi data layout");
_Static_assert(_Alignof(int64_t) == 8, "non-wasi data layout");
_Static_assert(_Alignof(uint64_t) == 8, "non-wasi data layout");
_Static_assert(_Alignof(wasi_ptr_t) == 4, "non-wasi data layout");

typedef uint32_t wasi_size_t;
_Static_assert(sizeof(wasi_size_t) == 4, "witx calculated size");
_Static_assert(_Alignof(wasi_size_t) == 4, "witx calculated align");

/**
 * Non-negative file size or length of a region within a file.
 */
typedef uint64_t wasi_filesize_t;
_Static_assert(sizeof(wasi_filesize_t) == 8, "witx calculated size");
_Static_assert(_Alignof(wasi_filesize_t) == 8, "witx calculated align");

/**
 * Timestamp in nanoseconds.
 */
typedef uint64_t wasi_timestamp_t;
_Static_assert(sizeof(wasi_timestamp_t) == 8, "witx calculated size");
_Static_assert(_Alignof(wasi_timestamp_t) == 8, "witx calculated align");

/**
 * Identifiers for clocks.
 */
typedef enum wasi_clockid : uint32_t {
    /**
     * The clock measuring real time. Time value zero corresponds with
     * 1970-01-01T00:00:00Z.
     */
    WASI_CLOCKID_REALTIME = 0,

    /**
     * The store-wide monotonic clock, which is defined as a clock measuring
     * real time, whose value cannot be adjusted and which cannot have negative
     * clock jumps. The epoch of this clock is undefined. The absolute time
     * value of this clock therefore has no meaning.
     */
    WASI_CLOCKID_MONOTONIC = 1,

    /**
     * The CPU-time clock associated with the current process.
     */
    WASI_CLOCKID_PROCESS_CPUTIME_ID = 2,

    /**
     * The CPU-time clock associated with the current thread.
     */
    WASI_CLOCKID_THREAD_CPUTIME_ID = 3,
} wasi_clockid_t;
_Static_assert(sizeof(wasi_clockid_t) == 4, "witx calculated size");
_Static_assert(_Alignof(wasi_clockid_t) == 4, "witx calculated align");

typedef enum wasi_errno : uint16_t {
    /**
     * No error occurred. System call completed successfully.
     */
    WASI_ERRNO_SUCCESS = 0,

    /**
     * Argument list too long.
     */
    WASI_ERRNO_2BIG = 1,

    /**
     * Permission denied.
     */
    WASI_ERRNO_ACCES = 2,

    /**
     * Address in use.
     */
    WASI_ERRNO_ADDRINUSE = 3,

    /**
     * Address not available.
     */
    WASI_ERRNO_ADDRNOTAVAIL = 4,

    /**
     * Address family not supported.
     */
    WASI_ERRNO_AFNOSUPPORT = 5,

    /**
     * Resource unavailable, or operation would block.
     */
    WASI_ERRNO_AGAIN = 6,

    /**
     * Connection already in progress.
     */
    WASI_ERRNO_ALREADY = 7,

    /**
     * Bad file descriptor.
     */
    WASI_ERRNO_BADF = 8,

    /**
     * Bad message.
     */
    WASI_ERRNO_BADMSG = 9,

    /**
     * Device or resource busy.
     */
    WASI_ERRNO_BUSY = 10,

    /**
     * Operation canceled.
     */
    WASI_ERRNO_CANCELED = 11,

    /**
     * No child processes.
     */
    WASI_ERRNO_CHILD = 12,

    /**
     * Connection aborted.
     */
    WASI_ERRNO_CONNABORTED = 13,

    /**
     * Connection refused.
     */
    WASI_ERRNO_CONNREFUSED = 14,

    /**
     * Connection reset.
     */
    WASI_ERRNO_CONNRESET = 15,

    /**
     * Resource deadlock would occur.
     */
    WASI_ERRNO_DEADLK = 16,

    /**
     * Destination address required.
     */
    WASI_ERRNO_DESTADDRREQ = 17,

    /**
     * Mathematics argument out of domain of function.
     */
    WASI_ERRNO_DOM = 18,

    /**
     * Reserved.
     */
    WASI_ERRNO_DQUOT = 19,

    /**
     * File exists.
     */
    WASI_ERRNO_EXIST = 20,

    /**
     * Bad address.
     */
    WASI_ERRNO_FAULT = 21,

    /**
     * File too large.
     */
    WASI_ERRNO_FBIG = 22,

    /**
     * Host is unreachable.
     */
    WASI_ERRNO_HOSTUNREACH = 23,

    /**
     * Identifier removed.
     */
    WASI_ERRNO_IDRM = 24,

    /**
     * Illegal byte sequence.
     */
    WASI_ERRNO_ILSEQ = 25,

    /**
     * Operation in progress.
     */
    WASI_ERRNO_INPROGRESS = 26,

    /**
     * Interrupted function.
     */
    WASI_ERRNO_INTR = 27,

    /**
     * Invalid argument.
     */
    WASI_ERRNO_INVAL = 28,

    /**
     * I/O error.
     */
    WASI_ERRNO_IO = 29,

    /**
     * Socket is connected.
     */
    WASI_ERRNO_ISCONN = 30,

    /**
     * Is a directory.
     */
    WASI_ERRNO_ISDIR = 31,

    /**
     * Too many levels of symbolic links.
     */
    WASI_ERRNO_LOOP = 32,

    /**
     * File descriptor value too large.
     */
    WASI_ERRNO_MFILE = 33,

    /**
     * Too many links.
     */
    WASI_ERRNO_MLINK = 34,

    /**
     * Message too large.
     */
    WASI_ERRNO_MSGSIZE = 35,

    /**
     * Reserved.
     */
    WASI_ERRNO_MULTIHOP = 36,

    /**
     * Filename too long.
     */
    WASI_ERRNO_NAMETOOLONG = 37,

    /**
     * Network is down.
     */
    WASI_ERRNO_NETDOWN = 38,

    /**
     * Connection aborted by network.
     */
    WASI_ERRNO_NETRESET = 39,

    /**
     * Network unreachable.
     */
    WASI_ERRNO_NETUNREACH = 40,

    /**
     * Too many files open in system.
     */
    WASI_ERRNO_NFILE = 41,

    /**
     * No buffer space available.
     */
    WASI_ERRNO_NOBUFS = 42,

    /**
     * No such device.
     */
    WASI_ERRNO_NODEV = 43,

    /**
     * No such file or directory.
     */
    WASI_ERRNO_NOENT = 44,

    /**
     * Executable file format error.
     */
    WASI_ERRNO_NOEXEC = 45,

    /**
     * No locks available.
     */
    WASI_ERRNO_NOLCK = 46,

    /**
     * Reserved.
     */
    WASI_ERRNO_NOLINK = 47,

    /**
     * Not enough space.
     */
    WASI_ERRNO_NOMEM = 48,

    /**
     * No message of the desired type.
     */
    WASI_ERRNO_NOMSG = 49,

    /**
     * Protocol not available.
     */
    WASI_ERRNO_NOPROTOOPT = 50,

    /**
     * No space left on device.
     */
    WASI_ERRNO_NOSPC = 51,

    /**
     * Function not supported.
     */
    WASI_ERRNO_NOSYS = 52,

    /**
     * The socket is not connected.
     */
    WASI_ERRNO_NOTCONN = 53,

    /**
     * Not a directory or a symbolic link to a directory.
     */
    WASI_ERRNO_NOTDIR = 54,

    /**
     * Directory not empty.
     */
    WASI_ERRNO_NOTEMPTY = 55,

    /**
     * State not recoverable.
     */
    WASI_ERRNO_NOTRECOVERABLE = 56,

    /**
     * Not a socket.
     */
    WASI_ERRNO_NOTSOCK = 57,

    /**
     * Not supported, or operation not supported on socket.
     */
    WASI_ERRNO_NOTSUP = 58,

    /**
     * Inappropriate I/O control operation.
     */
    WASI_ERRNO_NOTTY = 59,

    /**
     * No such device or address.
     */
    WASI_ERRNO_NXIO = 60,

    /**
     * Value too large to be stored in data type.
     */
    WASI_ERRNO_OVERFLOW = 61,

    /**
     * Previous owner died.
     */
    WASI_ERRNO_OWNERDEAD = 62,

    /**
     * Operation not permitted.
     */
    WASI_ERRNO_PERM = 63,

    /**
     * Broken pipe.
     */
    WASI_ERRNO_PIPE = 64,

    /**
     * Protocol error.
     */
    WASI_ERRNO_PROTO = 65,

    /**
     * Protocol not supported.
     */
    WASI_ERRNO_PROTONOSUPPORT = 66,

    /**
     * Protocol wrong type for socket.
     */
    WASI_ERRNO_PROTOTYPE = 67,

    /**
     * Result too large.
     */
    WASI_ERRNO_RANGE = 68,

    /**
     * Read-only file system.
     */
    WASI_ERRNO_ROFS = 69,

    /**
     * Invalid seek.
     */
    WASI_ERRNO_SPIPE = 70,

    /**
     * No such process.
     */
    WASI_ERRNO_SRCH = 71,

    /**
     * Reserved.
     */
    WASI_ERRNO_STALE = 72,

    /**
     * Connection timed out.
     */
    WASI_ERRNO_TIMEDOUT = 73,

    /**
     * Text file busy.
     */
    WASI_ERRNO_TXTBSY = 74,

    /**
     * Cross-device link.
     */
    WASI_ERRNO_XDEV = 75,

    /**
     * Extension: Capabilities insufficient.
     */
    WASI_ERRNO_NOTCAPABLE = 76,
} wasi_errno_t;
_Static_assert(sizeof(wasi_errno_t) == 2, "witx calculated size");
_Static_assert(_Alignof(wasi_errno_t) == 2, "witx calculated align");

/**
 * File descriptor rights, determining which actions may be performed.
 */
typedef enum wasi_rights : uint64_t {
    /**
     * The right to invoke `fd_datasync`.
     * If `path_open` is set, includes the right to invoke
     * `path_open` with `fdflags::dsync`.
     */
    WASI_RIGHTS_FD_DATASYNC = BIT0,

    /**
     * The right to invoke `fd_read` and `sock_recv`.
     * If `rights::fd_seek` is set, includes the right to invoke `fd_pread`.
     */
    WASI_RIGHTS_FD_READ = BIT1,

    /**
     * The right to invoke `fd_seek`. This flag implies `rights::fd_tell`.
     */
    WASI_RIGHTS_FD_SEEK = BIT2,

    /**
     * The right to invoke `fd_fdstat_set_flags`.
     */
    WASI_RIGHTS_FD_FDSTAT_SET_FLAGS = BIT3,

    /**
     * The right to invoke `fd_sync`.
     * If `path_open` is set, includes the right to invoke
     * `path_open` with `fdflags::rsync` and `fdflags::dsync`.
     */
    WASI_RIGHTS_FD_SYNC = BIT4,

    /**
     * The right to invoke `fd_seek` in such a way that the file offset
     * remains unaltered (i.e., `whence::cur` with offset zero), or to
     * invoke `fd_tell`.
     */
    WASI_RIGHTS_FD_TELL = BIT5,

    /**
     * The right to invoke `fd_write` and `sock_send`.
     * If `rights::fd_seek` is set, includes the right to invoke `fd_pwrite`.
     */
    WASI_RIGHTS_FD_WRITE = BIT6,

    /**
     * The right to invoke `fd_advise`.
     */
    WASI_RIGHTS_FD_ADVISE = BIT7,

    /**
     * The right to invoke `fd_allocate`.
     */
    WASI_RIGHTS_FD_ALLOCATE = BIT8,

    /**
     * The right to invoke `path_create_directory`.
     */
    WASI_RIGHTS_PATH_CREATE_DIRECTORY = BIT9,

    /**
     * If `path_open` is set, the right to invoke `path_open` with `oflags::creat`.
     */
    WASI_RIGHTS_PATH_CREATE_FILE = BIT10,

    /**
     * The right to invoke `path_link` with the file descriptor as the
     * source directory.
     */
    WASI_RIGHTS_PATH_LINK_SOURCE = BIT11,

    /**
     * The right to invoke `path_link` with the file descriptor as the
     * target directory.
     */
    WASI_RIGHTS_PATH_LINK_TARGET = BIT12,

    /**
     * The right to invoke `path_open`.
     */
    WASI_RIGHTS_PATH_OPEN = BIT13,

    /**
     * The right to invoke `fd_readdir`.
     */
    WASI_RIGHTS_FD_READDIR = BIT14,

    /**
     * The right to invoke `path_readlink`.
     */
    WASI_RIGHTS_PATH_READLINK = BIT15,

    /**
     * The right to invoke `path_rename` with the file descriptor as the source
     * directory.
     */
    WASI_RIGHTS_PATH_RENAME_SOURCE = BIT16,

    /**
     * The right to invoke `path_rename` with the file descriptor as the target
     * directory.
     */
    WASI_RIGHTS_PATH_RENAME_TARGET = BIT17,

    /**
     * The right to invoke `path_filestat_get`.
     */
    WASI_RIGHTS_PATH_FILESTAT_GET = BIT18,

    /**
     * The right to change a file's size (there is no `path_filestat_set_size`).
     * If `path_open` is set, includes the right to invoke `path_open` with
     * `oflags::trunc`.
     */
    WASI_RIGHTS_PATH_FILESTAT_SET_SIZE = BIT19,

    /**
     * The right to invoke `path_filestat_set_times`.
     */
    WASI_RIGHTS_PATH_FILESTAT_SET_TIMES = BIT20,

    /**
     * The right to invoke `fd_filestat_get`.
     */
    WASI_RIGHTS_FD_FILESTAT_GET = BIT21,

    /**
     * The right to invoke `fd_filestat_set_size`.
     */
    WASI_RIGHTS_FD_FILESTAT_SET_SIZE = BIT22,

    /**
     * The right to invoke `fd_filestat_set_times`.
     */
    WASI_RIGHTS_FD_FILESTAT_SET_TIMES = BIT23,

    /**
     * The right to invoke `path_symlink`.
     */
    WASI_RIGHTS_PATH_SYMLINK = BIT24,

    /**
     * The right to invoke `path_remove_directory`.
     */
    WASI_RIGHTS_PATH_REMOVE_DIRECTORY = BIT25,

    /**
     * The right to invoke `path_unlink_file`.
     */
    WASI_RIGHTS_PATH_UNLINK_FILE = BIT26,

    /**
     * If `rights::fd_read` is set, includes the right to invoke `poll_oneoff` to
     * subscribe to `eventtype::fd_read`. If `rights::fd_write` is set, includes the
     * right to invoke `poll_oneoff` to subscribe to `eventtype::fd_write`.
     */
    WASI_RIGHTS_POLL_FD_READWRITE = BIT27,

    /**
     * The right to invoke `sock_shutdown`.
     */
    WASI_RIGHTS_SOCK_SHUTDOWN = BIT28,

    /**
     * The right to invoke `sock_accept`.
     */
    WASI_RIGHTS_SOCK_ACCEPT = BIT29,
} wasi_rights_t;


/**
 * A file descriptor handle.
 */
typedef int wasi_fd_t;
_Static_assert(sizeof(wasi_fd_t) == 4, "witx calculated size");
_Static_assert(_Alignof(wasi_fd_t) == 4, "witx calculated align");

/**
 * A region of memory for scatter/gather reads.
 */
typedef struct wasi_iovec {
    /**
     * The address of the buffer to be filled.
     */
    wasi_ptr_t buf;

    /**
     * The length of the buffer to be filled.
     */
    wasi_size_t buf_len;
} wasi_iovec_t;

_Static_assert(sizeof(wasi_iovec_t) == 8, "witx calculated size");
_Static_assert(_Alignof(wasi_iovec_t) == 4, "witx calculated align");
_Static_assert(offsetof(wasi_iovec_t, buf) == 0, "witx calculated offset");
_Static_assert(offsetof(wasi_iovec_t, buf_len) == 4, "witx calculated offset");

/**
 * Relative offset within a file.
 */
typedef int64_t wasi_filedelta_t;
_Static_assert(sizeof(wasi_filedelta_t) == 8, "witx calculated size");
_Static_assert(_Alignof(wasi_filedelta_t) == 8, "witx calculated align");

/**
 * The position relative to which to set the offset of the file descriptor.
 */
typedef enum wasi_whence : uint8_t {
    /**
     * Seek relative to start-of-file.
     */
    WASI_WHENCE_SET = 0,

    /**
     * Seek relative to current position.
     */
    WASI_WHENCE_CUR = 1,

    /**
     * Seek relative to end-of-file.
     */
    WASI_WHENCE_END = 2,
} wasi_whence_t;

_Static_assert(sizeof(wasi_whence_t) == 1, "witx calculated size");
_Static_assert(_Alignof(wasi_whence_t) == 1, "witx calculated align");

/**
 * A reference to the offset of a directory entry.
 *
 * The value 0 signifies the start of the directory.
 */
typedef uint64_t wasi_dircookie_t;
_Static_assert(sizeof(wasi_dircookie_t) == 8, "witx calculated size");
_Static_assert(_Alignof(wasi_dircookie_t) == 8, "witx calculated align");

/**
 * The type for the `dirent::d_namlen` field of `dirent` struct.
 */
typedef uint32_t wasi_dirnamlen_t;
_Static_assert(sizeof(wasi_dirnamlen_t) == 4, "witx calculated size");
_Static_assert(_Alignof(wasi_dirnamlen_t) == 4, "witx calculated align");

/**
 * File serial number that is unique within its file system.
 */
typedef uint64_t wasi_inode_t;
_Static_assert(sizeof(wasi_inode_t) == 8, "witx calculated size");
_Static_assert(_Alignof(wasi_inode_t) == 8, "witx calculated align");

/**
 * The type of a file descriptor or file.
 */
typedef enum wasi_filetype : uint8_t {
    /**
     * The type of the file descriptor or file is unknown or is different from any
     * of the other types specified.
     */
    WASI_FILETYPE_UNKNOWN = 0,

    /**
     * The file descriptor or file refers to a block device inode.
     */
    WASI_FILETYPE_BLOCK_DEVICE = 1,

    /**
     * The file descriptor or file refers to a character device inode.
     */
    WASI_FILETYPE_CHARACTER_DEVICE = 2,

    /**
     * The file descriptor or file refers to a directory inode.
     */
    WASI_FILETYPE_DIRECTORY = 3,

    /**
     * The file descriptor or file refers to a regular file inode.
     */
    WASI_FILETYPE_REGULAR_FILE = 4,

    /**
     * The file descriptor or file refers to a datagram socket.
     */
    WASI_FILETYPE_SOCKET_DGRAM = 5,

    /**
     * The file descriptor or file refers to a byte-stream socket.
     */
    WASI_FILETYPE_SOCKET_STREAM = 6,

    /**
     * The file refers to a symbolic link inode.
     */
    WASI_FILETYPE_SYMBOLIC_LINK = 7,
} wasi_filetype_t;
_Static_assert(sizeof(wasi_filetype_t) == 1, "witx calculated size");
_Static_assert(_Alignof(wasi_filetype_t) == 1, "witx calculated align");

/**
 * A directory entry.
 */
typedef struct wasi_dirent {
    /**
     * The offset of the next directory entry stored in this directory.
     */
    wasi_dircookie_t d_next;

    /**
     * The serial number of the file referred to by this directory entry.
     */
    wasi_inode_t d_ino;

    /**
     * The length of the name of the directory entry.
     */
    wasi_dirnamlen_t d_namlen;

    /**
     * The type of the file referred to by this directory entry.
     */
    wasi_filetype_t d_type;
} wasi_dirent_t;

_Static_assert(sizeof(wasi_dirent_t) == 24, "witx calculated size");
_Static_assert(_Alignof(wasi_dirent_t) == 8, "witx calculated align");
_Static_assert(offsetof(wasi_dirent_t, d_next) == 0, "witx calculated offset");
_Static_assert(offsetof(wasi_dirent_t, d_ino) == 8, "witx calculated offset");
_Static_assert(offsetof(wasi_dirent_t, d_namlen) == 16, "witx calculated offset");
_Static_assert(offsetof(wasi_dirent_t, d_type) == 20, "witx calculated offset");

/**
 * File or memory access pattern advisory information.
 */
typedef enum wasi_advice : uint8_t {
    /**
     * The application has no advice to give on its behavior with respect to the
     * specified data.
     */
    WASI_ADVICE_NORMAL = 0,

    /**
     * The application expects to access the specified data sequentially from lower
     * offsets to higher offsets.
     */
    WASI_ADVICE_SEQUENTIAL = 1,

    /**
     * The application expects to access the specified data in a random order.
     */
    WASI_ADVICE_RANDOM = 2,

    /**
     * The application expects to access the specified data in the near future.
     */
    WASI_ADVICE_WILLNEED = 3,

    /**
     * The application expects that it will not access the specified data in the
     * near future.
     */
    WASI_ADVICE_DONTNEED = 4,

    /**
     * The application expects to access the specified data once and then not reuse
     * it thereafter.
     */
    WASI_ADVICE_NOREUSE = 5,
} wasi_advice_t;

_Static_assert(sizeof(wasi_advice_t) == 1, "witx calculated size");
_Static_assert(_Alignof(wasi_advice_t) == 1, "witx calculated align");


/**
 * File descriptor flags.
 */
typedef enum wasi_fdflags : uint16_t {
    /**
     * Append mode: Data written to the file is always appended to the file's end.
     */
    WASI_FDFLAGS_APPEND = BIT0,

    /**
     * Write according to synchronized I/O data integrity completion. Only the data
     * stored in the file is synchronized.
     */
    WASI_FDFLAGS_DSYNC = BIT1,

    /**
     * Non-blocking mode.
     */
    WASI_FDFLAGS_NONBLOCK = BIT2,

    /**
     * Synchronized read I/O operations.
     */
    WASI_FDFLAGS_RSYNC = BIT3,

    /**
     * Write according to synchronized I/O file integrity completion. In
     * addition to synchronizing the data stored in the file, the implementation
     * may also synchronously update the file's metadata.
     */
    WASI_FDFLAGS_SYNC = BIT4,
} wasi_fdflags_t;

/**
 * File descriptor attributes.
 */
typedef struct wasi_fdstat {
    /**
     * File type.
     */
    wasi_filetype_t fs_filetype;

    /**
     * File descriptor flags.
     */
    wasi_fdflags_t fs_flags;

    /**
     * Rights that apply to this file descriptor.
     */
    wasi_rights_t fs_rights_base;

    /**
     * Maximum set of rights that may be installed on new file descriptors that
     * are created through this file descriptor, e.g., through `path_open`.
     */
    wasi_rights_t fs_rights_inheriting;
} wasi_fdstat_t;

_Static_assert(sizeof(wasi_fdstat_t) == 24, "witx calculated size");
_Static_assert(_Alignof(wasi_fdstat_t) == 8, "witx calculated align");
_Static_assert(offsetof(wasi_fdstat_t, fs_filetype) == 0, "witx calculated offset");
_Static_assert(offsetof(wasi_fdstat_t, fs_flags) == 2, "witx calculated offset");
_Static_assert(offsetof(wasi_fdstat_t, fs_rights_base) == 8, "witx calculated offset");
_Static_assert(offsetof(wasi_fdstat_t, fs_rights_inheriting) == 16, "witx calculated offset");

/**
 * Identifier for a device containing a file system. Can be used in combination
 * with `inode` to uniquely identify a file or directory in the filesystem.
 */
typedef uint64_t wasi_device_t;
_Static_assert(sizeof(wasi_device_t) == 8, "witx calculated size");
_Static_assert(_Alignof(wasi_device_t) == 8, "witx calculated align");

/**
 * Which file time attributes to adjust.
 */
typedef enum wasi_fstflags : uint16_t {
    /**
     * Adjust the last data access timestamp to the value stored in
     * `filestat::atim`.
     */
    WASI_FSTFLAGS_ATIM = BIT0,

    /**
     * Adjust the last data access timestamp to the time of clock
     * `clockid::realtime`.
     */
    WASI_FSTFLAGS_ATIM_NOW = BIT1,

    /**
     * Adjust the last data modification timestamp to the value stored in
     * `filestat::mtim`.
     */
    WASI_FSTFLAGS_MTIM = BIT2,

    /**
     * Adjust the last data modification timestamp to the time of clock
     * `clockid::realtime`.
     */
    WASI_FSTFLAGS_MTIM_NOW = BIT3,
} wasi_fstflags_t;


/**
 * Flags determining the method of how paths are resolved.
 */
typedef enum wasi_lookupflags : uint32_t {
    /**
     * As long as the resolved path corresponds to a symbolic link, it is expanded.
     */
    WASI_LOOKUPFLAGS_SYMLINK_FOLLOW = BIT0,
} wasi_lookupflags_t;


/**
 * Open flags used by `path_open`.
 */
typedef enum wasi_oflags : uint16_t {
    /**
     * Create file if it does not exist.
     */
    WASI_OFLAGS_CREAT = BIT0,

    /**
     * Fail if not a directory.
     */
    WASI_OFLAGS_DIRECTORY = BIT1,

    /**
     * Fail if file already exists.
     */
    WASI_OFLAGS_EXCL = BIT2,

    /**
     * Truncate file to size 0.
     */
    WASI_OFLAGS_TRUNC = BIT3,
} wasi_oflags_t;

/**
 * Number of hard links to an inode.
 */
typedef uint64_t wasi_linkcount_t;
_Static_assert(sizeof(wasi_linkcount_t) == 8, "witx calculated size");
_Static_assert(_Alignof(wasi_linkcount_t) == 8, "witx calculated align");

/**
 * File attributes.
 */
typedef struct __wasi_filestat_t {
    /**
     * Device ID of device containing the file.
     */
    wasi_device_t dev;

    /**
     * File serial number.
     */
    wasi_inode_t ino;

    /**
     * File type.
     */
    wasi_filetype_t filetype;

    /**
     * Number of hard links to the file.
     */
    wasi_linkcount_t nlink;

    /**
     * For regular files, the file size in bytes. For symbolic links, the length
     * in bytes of the pathname contained in the symbolic link.
     */
    wasi_filesize_t size;

    /**
     * Last data access timestamp.
     */
    wasi_timestamp_t atim;

    /**
     * Last data modification timestamp.
     */
    wasi_timestamp_t mtim;

    /**
     * Last file status change timestamp.
     */
    wasi_timestamp_t ctim;
} __wasi_filestat_t;

_Static_assert(sizeof(__wasi_filestat_t) == 64, "witx calculated size");
_Static_assert(_Alignof(__wasi_filestat_t) == 8, "witx calculated align");
_Static_assert(offsetof(__wasi_filestat_t, dev) == 0, "witx calculated offset");
_Static_assert(offsetof(__wasi_filestat_t, ino) == 8, "witx calculated offset");
_Static_assert(offsetof(__wasi_filestat_t, filetype) == 16, "witx calculated offset");
_Static_assert(offsetof(__wasi_filestat_t, nlink) == 24, "witx calculated offset");
_Static_assert(offsetof(__wasi_filestat_t, size) == 32, "witx calculated offset");
_Static_assert(offsetof(__wasi_filestat_t, atim) == 40, "witx calculated offset");
_Static_assert(offsetof(__wasi_filestat_t, mtim) == 48, "witx calculated offset");
_Static_assert(offsetof(__wasi_filestat_t, ctim) == 56, "witx calculated offset");

/**
 * User-provided value that may be attached to objects that is retained when
 * extracted from the implementation.
 */
typedef uint64_t wasi_userdata_t;
_Static_assert(sizeof(wasi_userdata_t) == 8, "witx calculated size");
_Static_assert(_Alignof(wasi_userdata_t) == 8, "witx calculated align");

/**
 * Type of a subscription to an event or its occurrence.
 */
typedef enum wasi_eventtype : uint8_t {
    /**
     * The time value of clock `subscription_clock::id` has
     * reached timestamp `subscription_clock::timeout`.
     */
    WASI_EVENTTYPE_CLOCK = 0,

    /**
     * File descriptor `subscription_fd_readwrite::file_descriptor` has data
     * available for reading. This event always triggers for regular files.
     */
    WASI_EVENTTYPE_FD_READ = 1,

    /**
     * File descriptor `subscription_fd_readwrite::file_descriptor` has capacity
     * available for writing. This event always triggers for regular files.
     */
    WASI_EVENTTYPE_FD_WRITE = 2,
} wasi_eventtype_t;

_Static_assert(sizeof(wasi_eventtype_t) == 1, "witx calculated size");
_Static_assert(_Alignof(wasi_eventtype_t) == 1, "witx calculated align");

/**
 * The state of the file descriptor subscribed to with
 * `eventtype::fd_read` or `eventtype::fd_write`.
 */
typedef enum wasi_eventrwflags_t : uint16_t {
    /**
     * The peer of this socket has closed or disconnected.
     */
    WASI_EVENTRWFLAGS_FD_READWRITE_HANGUP = BIT0,
} wasi_eventrwflags_t;


/**
 * The contents of an `event` when type is `eventtype::fd_read` or
 * `eventtype::fd_write`.
 */
typedef struct wasi_event_fd_readwrite_t {
    /**
     * The number of bytes available for reading or writing.
     */
    wasi_filesize_t nbytes;

    /**
     * The state of the file descriptor.
     */
    wasi_eventrwflags_t flags;
} wasi_event_fd_readwrite_t;

_Static_assert(sizeof(wasi_event_fd_readwrite_t) == 16, "witx calculated size");
_Static_assert(_Alignof(wasi_event_fd_readwrite_t) == 8, "witx calculated align");
_Static_assert(offsetof(wasi_event_fd_readwrite_t, nbytes) == 0, "witx calculated offset");
_Static_assert(offsetof(wasi_event_fd_readwrite_t, flags) == 8, "witx calculated offset");

/**
 * An event that occurred.
 */
typedef struct __wasi_event_t {
    /**
     * User-provided value that got attached to `subscription::userdata`.
     */
    wasi_userdata_t userdata;

    /**
     * If non-zero, an error that occurred while processing the subscription
     * request.
     */
    wasi_errno_t error;

    /**
     * The type of event that occured
     */
    wasi_eventtype_t type;

    /**
     * The contents of the event, if it is an `eventtype::fd_read` or
     * `eventtype::fd_write`. `eventtype::clock` events ignore this field.
     */
    wasi_event_fd_readwrite_t fd_readwrite;
} wasi_event_t;

_Static_assert(sizeof(wasi_event_t) == 32, "witx calculated size");
_Static_assert(_Alignof(wasi_event_t) == 8, "witx calculated align");
_Static_assert(offsetof(wasi_event_t, userdata) == 0, "witx calculated offset");
_Static_assert(offsetof(wasi_event_t, error) == 8, "witx calculated offset");
_Static_assert(offsetof(wasi_event_t, type) == 10, "witx calculated offset");
_Static_assert(offsetof(wasi_event_t, fd_readwrite) == 16, "witx calculated offset");

/**
 * Flags determining how to interpret the timestamp provided in
 * `subscription_clock::timeout`.
 */
typedef enum wasi_subclockflags : uint16_t {
    /**
     * If set, treat the timestamp provided in
     * `subscription_clock::timeout` as an absolute timestamp of clock
     * `subscription_clock::id`. If clear, treat the timestamp
     * provided in `subscription_clock::timeout` relative to the
     * current time value of clock `subscription_clock::id`.
     */
    WASI_SUBCLOCKFLAGS_SUBSCRIPTION_CLOCK_ABSTIME = BIT0,
} wasi_subclockflags_t;

/**
 * The contents of a `subscription` when type is `eventtype::clock`.
 */
typedef struct wasi_subscription_clock {
    /**
     * The clock against which to compare the timestamp.
     */
    wasi_clockid_t id;

    /**
     * The absolute or relative timestamp.
     */
    wasi_timestamp_t timeout;

    /**
     * The amount of time that the implementation may wait additionally
     * to coalesce with other events.
     */
    wasi_timestamp_t precision;

    /**
     * Flags specifying whether the timeout is absolute or relative
     */
    wasi_subclockflags_t flags;
} wasi_subscription_clock_t;

_Static_assert(sizeof(wasi_subscription_clock_t) == 32, "witx calculated size");
_Static_assert(_Alignof(wasi_subscription_clock_t) == 8, "witx calculated align");
_Static_assert(offsetof(wasi_subscription_clock_t, id) == 0, "witx calculated offset");
_Static_assert(offsetof(wasi_subscription_clock_t, timeout) == 8, "witx calculated offset");
_Static_assert(offsetof(wasi_subscription_clock_t, precision) == 16, "witx calculated offset");
_Static_assert(offsetof(wasi_subscription_clock_t, flags) == 24, "witx calculated offset");

/**
 * The contents of a `subscription` when type is type is
 * `eventtype::fd_read` or `eventtype::fd_write`.
 */
typedef struct __wasi_subscription_fd_readwrite_t {
    /**
     * The file descriptor on which to wait for it to become ready for reading or
     * writing.
     */
    wasi_fd_t file_descriptor;
} wasi_subscription_fd_readwrite_t;

_Static_assert(sizeof(wasi_subscription_fd_readwrite_t) == 4, "witx calculated size");
_Static_assert(_Alignof(wasi_subscription_fd_readwrite_t) == 4, "witx calculated align");
_Static_assert(offsetof(wasi_subscription_fd_readwrite_t, file_descriptor) == 0, "witx calculated offset");

/**
 * Subscription to an event.
 */
typedef struct __wasi_subscription_t {
    /**
     * User-provided value that is attached to the subscription in the
     * implementation and returned through `event::userdata`.
     */
    wasi_userdata_t userdata;

    /**
     * The type of the event to which to subscribe, and its contents
     */
    uint8_t tag;
    union {
        wasi_subscription_clock_t clock;
        wasi_subscription_fd_readwrite_t fd_read;
        wasi_subscription_fd_readwrite_t fd_write;
    };
} wasi_subscription_t;

_Static_assert(sizeof(wasi_subscription_t) == 48, "witx calculated size");
_Static_assert(_Alignof(wasi_subscription_t) == 8, "witx calculated align");
_Static_assert(offsetof(wasi_subscription_t, userdata) == 0, "witx calculated offset");
_Static_assert(offsetof(wasi_subscription_t, tag) == 8, "witx calculated offset");

/**
 * Exit code generated by a process when exiting.
 */
typedef uint32_t wasi_exitcode_t;
_Static_assert(sizeof(wasi_exitcode_t) == 4, "witx calculated size");
_Static_assert(_Alignof(wasi_exitcode_t) == 4, "witx calculated align");

/**
 * Flags provided to `sock_recv`.
 */
typedef enum wasi_riflags : uint16_t {
    /**
     * Returns the message without removing it from the socket's receive queue.
     */
    WASI_RIFLAGS_RECV_PEEK = BIT0,

    /**
     * On byte-stream sockets, block until the full amount of data can be returned.
     */
    WASI_RIFLAGS_RECV_WAITALL = BIT1,
} wasi_riflags_t;

/**
 * Flags returned by `sock_recv`.
 */
typedef enum wasi_roflags : uint16_t {
    /**
     * Returned by `sock_recv`: Message data has been truncated.
     */
    WASI_ROFLAGS_RECV_DATA_TRUNCATED = BIT0,
} wasi_roflags_t;

/**
 * Flags provided to `sock_send`. As there are currently no flags
 * defined, it must be set to zero.
 */
typedef uint16_t wasi_siflags_t;
_Static_assert(sizeof(wasi_siflags_t) == 2, "witx calculated size");
_Static_assert(_Alignof(wasi_siflags_t) == 2, "witx calculated align");

/**
 * Which channels on a socket to shut down.
 */
typedef enum wasi_sdflags : uint8_t {
    /**
     * Disables further receive operations.
     */
    WASI_SDFLAGS_RD = BIT0,

    /**
     * Disables further send operations.
     */
    WASI_SDFLAGS_WR = BIT1,
} wasi_sdflags_t;

/**
 * Identifiers for preopened capabilities.
 */
typedef enum wasi_preopentype : uint8_t {
    /**
    * A pre-opened directory.
    */
    WASI_PREOPENTYPE_DIR = 0,
} wasi_preopentype_t;

_Static_assert(sizeof(wasi_preopentype_t) == 1, "witx calculated size");
_Static_assert(_Alignof(wasi_preopentype_t) == 1, "witx calculated align");

/**
 * Information about a pre-opened capability.
 */
typedef struct wasi_prestat {
    uint8_t tag;
    union {
        /**
         * The contents of a $prestat when type is `preopentype::dir`.
         */
        struct {
            /**
             * The length of the directory name for use with `fd_prestat_dir_name`.
             */
            wasi_size_t pr_name_len;
        } dir;
    };
} wasi_prestat_t;

_Static_assert(sizeof(wasi_prestat_t) == 8, "witx calculated size");
_Static_assert(_Alignof(wasi_prestat_t) == 4, "witx calculated align");
