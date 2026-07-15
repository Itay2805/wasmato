#include "wasi.h"

#include "alloc/alloc.h"
#include "lib/assert.h"
#include "lib/atomic.h"
#include "lib/defs.h"
#include "lib/except.h"
#include "lib/list.h"
#include "lib/log.h"
#include "lib/string.h"
#include "lib/syscall.h"
#include "lib/tsc.h"
#include "lib/stb_ds.h"
#include "uapi/wait.h"
#include "wasm/errno.h"
#include "wasm/file.h"
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

static wasi_errno_t wasi_fd_write(
    void* memory_base, void* state_base, 
    wasi_fd_t fd, 
    wasm_ptr_t _iovs, wasi_size_t iovs_len, 
    wasm_ptr_t _retptr0
) {
    const wasi_ciovec_t* iovs = memory_base + _iovs;
    wasi_size_t* retptr0 = memory_base + _retptr0;

    // get the fd
    file_t* file = wasm_proc_get_fd(wasm_current_proc(state_base), fd);
    if (file == nullptr) {
        WARN("wasi_fd_write: got invalid fd %d", fd);
        return WASI_ERRNO_BADF;
    }

    // check the capability
    if (!file_is_capable(file, WASI_RIGHTS_FD_WRITE)) {
        WARN("wasi_fd_write: got uncapable file %d", fd);
        return WASI_ERRNO_NOTCAPABLE;
    }

    // TODO: perform the real write by using wasi-ipc to the channel of the file
    for (size_t i = 0; i < iovs_len; i++) {
        void* buf = memory_base + iovs[i].buf;
        sys_debug_print(buf, iovs[i].buf_len);
        *retptr0 += iovs[i].buf_len;
    }

    file_put(file);

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
    wasi_fdstat_t* retptr0 = memory_base + _retptr0;

    // get the fd
    file_t* file = wasm_proc_get_fd(wasm_current_proc(state_base), fd);
    if (file == nullptr) {
        WARN("wasi_fd_fdstat_get: got invalid fd %d", fd);
        return WASI_ERRNO_BADF;
    }

    *retptr0 = file->fdstat;

    file_put(file);

    return WASI_ERRNO_SUCCESS;
}

static wasi_errno_t wasi_fd_close(void* memory_base, void* state_base, wasi_fd_t fd) {
    if (wasm_proc_close_fd(wasm_current_proc(state_base), fd)) {
        return WASI_ERRNO_SUCCESS;
    } else {
        return WASI_ERRNO_BADF;
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FD polling
////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef uint64_t wasi_userdata_t;

typedef enum wasi_subclockflags : uint16_t {
    WASI_SUBCLOCKFLAGS_SUBSCRIPTION_CLOCK_ABSTIME = BIT0,
} wasi_subclockflags_t;

typedef struct wasi_subscription_clock {
    wasi_clockid_t id;
    wasi_timestamp_t timeout;
    wasi_timestamp_t precision;
    wasi_subclockflags_t flags;
} wasi_subscription_clock_t;

typedef struct wasi_subscription_fd_readwrite {
    wasi_fd_t file_descriptor;
} wasi_subscription_fd_readwrite_t;

typedef enum wasi_eventtype : uint8_t {
    WASI_EVENTTYPE_CLOCK = 0,
    WASI_EVENTTYPE_FD_READ = 1,
    WASI_EVENTTYPE_FD_WRITE = 2,
} wasi_eventtype_t;

typedef struct wasi_subscription {
    wasi_userdata_t userdata;
    struct {
        wasi_eventtype_t tag;
        union {
            wasi_subscription_clock_t clock;
            wasi_subscription_fd_readwrite_t fd_read;
            wasi_subscription_fd_readwrite_t fd_write;
        };
    };
} wasi_subscription_t;

typedef enum wasi_eventrwflags : uint16_t {
    WASI_EVENTRWFLAGS_FD_READWRITE_HANGUP = BIT0,
} wasi_eventrwflags_t;

typedef struct wasi_event_fd_readwrite_t {
    wasi_filesize_t nbytes;
    wasi_eventrwflags_t flags;
} wasi_event_fd_readwrite_t;

typedef struct wasi_event {
    wasi_userdata_t userdata;
    wasi_errno_t error;
    wasi_eventtype_t type;
    wasi_event_fd_readwrite_t fd_readwrite;
} wasi_event_t;

static wasi_errno_t wasi_poll_oneoff(
    void* memory_base, void* state_base, 
    wasm_ptr_t _in, wasm_ptr_t _out, wasi_size_t nsubscriptions, 
    wasm_ptr_t _retptr0
) {
    wasi_errno_t status = WASI_ERRNO_SUCCESS;

    // must have at least one entry waiting
    if (nsubscriptions == 0) {
        return WASI_ERRNO_INVAL;
    }

    // allocate the entries on the heap
    // TODO: stack cache? per thread cache? maybe even actually just cache the allocation
    //       per thread with the assumption that it is going to be used more?
    size_t wait_entries_len;
    if (__builtin_add_overflow(sizeof(wait_entry_t), nsubscriptions, &wait_entries_len)) {
        return WASI_ERRNO_NOMEM;
    }
    wait_entry_t* wait_entries = mem_alloc(wait_entries_len);
    size_t entry_count = 0;

    wasm_proc_t* proc = wasm_current_proc(state_base);

    const wasi_subscription_t* in_list = memory_base + _in;
    wasi_event_t* out_list = memory_base + _out;
    wasi_size_t* retptr0 = memory_base + _retptr0;

    // 
    // Prepare all the waiters, this will potentially already find
    // ready entries that we will return right away instead of going 
    // to sleep
    //
    wasi_size_t ready = 0;
    uint64_t min_deadline = -1;
    wasi_subscription_t deadline_sub;
    for (size_t i = 0; i < nsubscriptions; i++) {
        wasi_subscription_t in = in_list[i];

        //
        // Handle clock subscriptions, these basically gather 
        // into a min deadline for the wait
        //
        if (in.tag == WASI_EVENTTYPE_CLOCK) {
            // TODO: for now we only support monotonic clock
            if (in.clock.id != WASI_CLOCKID_MONOTONIC) {
                status = WASI_ERRNO_INVAL;
                WARN("wasi_poll_oneoff: invalid clock %d", in.clock.id);
                goto cleanup;
            }

            // calculate the timeout for this clock
            uint64_t deadline;
            if (in.clock.flags & WASI_SUBCLOCKFLAGS_SUBSCRIPTION_CLOCK_ABSTIME) {
                deadline = ns_to_tsc(in.clock.timeout);
            } else {
                deadline = tsc_ns_deadline(in.clock.timeout);
            }
            
            // if its smaller than the current deadline then use it
            // TODO: is this actually good enough? should we have a proper
            //       list of deadlines so we can go over them?
            if (min_deadline > deadline) {
                min_deadline = deadline;
                deadline_sub = in;
            }
            continue;
        }

        // make sure it is a known value
        if (in.tag != WASI_EVENTTYPE_FD_READ && in.tag != WASI_EVENTTYPE_FD_WRITE) {
            status = WASI_ERRNO_INVAL;
            WARN("wasi_poll_oneoff: got invalid tag %d", in.tag);
            goto cleanup;
        }

        // get the file
        file_t* file = wasm_proc_get_fd(proc, in.fd_read.file_descriptor);
        if (file == nullptr) {
            // invalid file, set error
            wasi_event_t* out = &out_list[ready++];
            out->userdata = in.userdata;
            out->error = WASI_ERRNO_BADF;
            out->type = in.tag;
            continue;
        }

        // set the signal mask, we always want to watch 
        // for a closed file because that will fuck us up
        uint64_t mask = FILE_SIGNAL_CLOSED;
        wasi_rights_t rights = WASI_RIGHTS_POLL_FD_READWRITE;
        if (in.tag == WASI_EVENTTYPE_FD_READ) {
            mask |= FILE_SIGNAL_READ_READY;
            rights |= WASI_RIGHTS_FD_READ;
        } else if (in.tag == WASI_EVENTTYPE_FD_WRITE) {
            mask |= FILE_SIGNAL_WRITE_READY;
            rights |= WASI_RIGHTS_FD_WRITE;
        }

        if (!file_is_capable(file, rights)) {
            // the file is not capable for polling 
            // on the given poll type
            wasi_event_t* out = &out_list[ready++];
            out->userdata = in.userdata;
            out->error = WASI_ERRNO_NOTCAPABLE;
            out->type = in.tag;

            file_put(file);
            continue;
        }        

        // check if the signal was set
        uint64_t value = atomic_load_acquire(&file->signals);
        if (value & mask) {
            // the signal is already set, move it to the 
            // ready output right away
            wasi_event_t* out = &out_list[ready++];
            out->userdata = in.userdata;
            out->error = WASI_ERRNO_SUCCESS;
            out->type = in.tag;

            // if the file was closed return BADF so 
            // the caller knows that
            if (value & FILE_SIGNAL_CLOSED) {
                out->error = WASI_ERRNO_BADF;
            }

            // TODO: how to set nbytes? how to set HUB?

            file_put(file);
            continue;
        }

        // we already have someone ready, we are not going 
        // to actually save it
        if (ready != 0) {
            file_put(file);
            continue;
        }

        // setup the wait entry
        wait_entry_t* entry = &wait_entries[entry_count++];
        entry->key_size = WAIT_KEY_UINT32;
        entry->key = &file->signals;
        entry->mask = mask;
        entry->old = value;
        entry->user_data = i;
    }

    // as long as nothing is ready, wait on stuff
    while (ready == 0) {
        wait_status_t ws = sys_atomic_wait(wait_entries, entry_count, min_deadline);
        if (ws == WAIT_STATUS_OUT_OF_MEMORY) {
            status = WASI_ERRNO_NOMEM;
            goto cleanup;
        }

        // if we are past the deadline then add it as an event
        // TODO: should we maybe do a full pass and find all the timeouts we got instead? 
        if (tsc_check_deadline(min_deadline)) {
            wasi_event_t* event = &out_list[ready++];
            event->error = WASI_ERRNO_SUCCESS;
            event->type = WASI_EVENTTYPE_CLOCK;
            event->userdata = deadline_sub.userdata;
        }

        // go over the wait entries and see if anything changed
        for (int i = 0; i < entry_count; i++) {
            wait_entry_t* entry = &wait_entries[i];

            // check if the value has changed or we 
            // got a spurious wakeup
            uint64_t value = atomic_load_acquire((_Atomic(uint32_t)*)entry->key);
            if (value & entry->mask) {
                wasi_event_t* event = &out_list[ready++];

                // NOTE: this is technically accessed non-atomically with the prepare loop
                //       so someone could have changed both, for now it does not matter, 
                //       especially for the user pointer, but in the future we should at 
                //       least remember the tag so we can handle it properly.
                event->userdata = in_list[entry->user_data].userdata;
                event->type = in_list[entry->user_data].tag;
                event->error = WASI_ERRNO_SUCCESS;
                event->fd_readwrite.nbytes = 1;
                event->fd_readwrite.flags = 0;

                if (value & FILE_SIGNAL_CLOSED) {
                    event->error = WASI_ERRNO_BADF;
                }

            } else {
                // update the value since we saw the new stuff
                entry->old = value;
            }
        }
    }

cleanup:
    // remove the ref count of all the files we are waiting on
    for (int i = 0; i < entry_count; i++) {
        file_t* file = containerof(wait_entries[i].key, file_t, signals);
        file_put(file);
    }

    // free the wait entries
    mem_free(wait_entries);

    // return the amount of ready entries
    *retptr0 = ready;

    return status;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Time handling
////////////////////////////////////////////////////////////////////////////////////////////////////////////

static wasi_errno_t wasi_clock_time_get(
    void* memory_base, void* state_base, 
    wasi_clockid_t id, wasi_timestamp_t precision, 
    wasm_ptr_t _retptr0
) {
    wasi_timestamp_t* retptr0 = memory_base + _retptr0;

    switch (id) {
        case WASI_CLOCKID_MONOTONIC: {
            *retptr0 = tsc_to_ns(get_tsc());
        } break;

        default:
            return WASI_ERRNO_NOTSUP;
    }

    return WASI_ERRNO_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Process and thread handling
////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void wasi_proc_exit(void* memory_base, void* state_base, wasi_exitcode_t rval) {
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
