#include "wasi.h"
#include "alloc/alloc.h"
#include "lib/defs.h"
#include "lib/list.h"
#include "lib/log.h"
#include "lib/syscall.h"
#include "lib/tsc.h"
#include "proc/handle.h"
#include "proc/object.h"
#include "proc/proc.h"
#include "proc/thread.h"
#include "runtime.h"
#include "uapi/wait.h"
#include "wasi/file.h"
#include "wasi/wasip1.h"
#include "wasm/wasm.h"
#include "lib/cpp_magic.h"
#include "lib/string.h"
#include <stdint.h>

//----------------------------------------------------------------------------------------------------------------------
// Clocks
//----------------------------------------------------------------------------------------------------------------------

static wasi_errno_t wasi_clock_time_get(
    void* memory_base, void* state_base,
    wasi_clockid_t id, 
    wasi_timestamp_t precision, 
    wasi_ptr_t retptr0
) {
    wasi_timestamp_t result;
    switch (id) {
        case WASI_CLOCKID_MONOTONIC:
            result = tsc_to_ns(get_tsc());
            break;

        default: 
            return WASI_ERRNO_INVAL;
    }

    if (!safe_copy(memory_base + retptr0, &result, sizeof(result))) {
        return WASI_ERRNO_FAULT;
    }

    return WASI_ERRNO_SUCCESS;
}

//----------------------------------------------------------------------------------------------------------------------
// File descriptors
//----------------------------------------------------------------------------------------------------------------------

static wasi_errno_t wasi_fd_close(
    void* memory_base, void* state_base,
    wasi_fd_t fd
) {
    wasm_proc_t* proc = wasm_current_proc(state_base);

    // NOTE: unlike most fd operations, this does not assume a wasi-file, and is 
    //       used for closing all kind of handles
    if (!handle_table_close(&proc->handles, fd)) {
        return WASI_ERRNO_BADF;
    }

    return WASI_ERRNO_SUCCESS;
}

static wasi_errno_t wasi_fd_fdstat_get(
    void* memory_base, void* state_base,
    wasi_fd_t fd, wasi_ptr_t retptr0
) {
    wasi_errno_t errno = WASI_ERRNO_SUCCESS;

    wasm_proc_t* proc = wasm_current_proc(state_base);
    object_t* object = handle_table_lookup(&proc->handles, fd).object;
    if (object == nullptr) {
        return WASI_ERRNO_BADF;
    }

    wasi_file_t* file = wasi_file_from_object(object);
    WASI_CHECK(file != nullptr, INVAL);

    WASI_CHECK(
        safe_copy(memory_base + retptr0, &file->stats, sizeof(file->stats)), 
        FAULT
    );

cleanup:
    object_handle_put(object);
    return errno;
}

static wasi_errno_t wasi_fd_seek(
    void* memory_base, void* state_base,
    wasi_fd_t fd,
    wasi_filedelta_t offset,
    wasi_whence_t whence,
    wasi_ptr_t retptr0
) {
    wasi_errno_t errno = WASI_ERRNO_SUCCESS;
    
    wasm_proc_t* proc = wasm_current_proc(state_base);
    object_t* object = handle_table_lookup(&proc->handles, fd).object;
    if (object == nullptr) {
        return WASI_ERRNO_BADF;
    }

    wasi_file_t* file = wasi_file_from_object(object);
    WASI_CHECK(file != nullptr, INVAL);
    WASI_CHECK(wasi_file_is_capable(file, WASI_RIGHTS_FD_SEEK), NOTCAPABLE);

    // TODO: perform wasi-ipc 
    ERROR("TODO: wasi_fd_seek");

cleanup:
    object_put(object);
    return errno;
}

static wasi_errno_t wasi_fd_write(
    void* memory_base, void* state_base,
    wasi_fd_t fd,
    wasi_ptr_t _iovs, wasi_size_t iovs_len, 
    wasi_ptr_t retptr0
) {
    wasi_errno_t errno = WASI_ERRNO_SUCCESS;

    wasm_proc_t* proc = wasm_current_proc(state_base);
    object_t* object = handle_table_lookup(&proc->handles, fd).object;
    if (object == nullptr) {
        return WASI_ERRNO_BADF;
    }

    wasi_file_t* file = wasi_file_from_object(object);
    WASI_CHECK(file != nullptr, INVAL);
    WASI_CHECK(wasi_file_is_capable(file, WASI_RIGHTS_FD_WRITE), NOTCAPABLE);

    // TODO: perform wasi-ipc, for now some dummy code 
    //       just for stdout/stderr to work
    const wasi_iovec_t* iovs = memory_base + _iovs;
    wasi_size_t written = 0;
    for (size_t i = 0; i < iovs_len; i++) {
        sys_debug_print(memory_base + iovs[i].buf, iovs[i].buf_len);
        written += iovs[i].buf_len;
    }
    safe_copy(memory_base + retptr0, &written, sizeof(written));

cleanup:
    object_put(object);
    return errno;
} 

//----------------------------------------------------------------------------------------------------------------------
// Polling
//----------------------------------------------------------------------------------------------------------------------

static bool wasi_event_set_signal(
    wasi_event_t* event,
    const wasi_subscription_t* sub,
    uint32_t signals 
) {
    wasi_event_t data = {
        .userdata = sub->userdata,
        .error = WASI_ERRNO_SUCCESS,
        .type = sub->tag,
        .fd_readwrite.flags = 0,
        .fd_readwrite.nbytes = 0,
    };

    // if the file was closed return BADF so 
    // the caller knows that
    if (signals & SIGNAL_CLOSED) {
        data.error = WASI_ERRNO_BADF;
    }

    // the peer got closed, we got a hangup
    if (signals & SIGNAL_PEER_CLOSED) {
        data.fd_readwrite.flags |= WASI_EVENTRWFLAGS_FD_READWRITE_HANGUP;
    }

    return !safe_copy(event, &data, sizeof(data));
}

static bool wasi_event_set_errno(
    wasi_event_t* event,
    const wasi_subscription_t* sub,
    wasi_errno_t errno
) {
    wasi_event_t data = {
        .userdata = sub->userdata,
        .error = errno,
        .type = sub->tag
    };

    if (errno != WASI_ERRNO_SUCCESS) {
        ERROR("wasi: poll event -> %d", errno);
    }

    return !safe_copy(event, &data, sizeof(data));
}

static wasi_errno_t wasi_poll_oneoff(
    void* memory_base, void* state_base, 
    wasi_ptr_t in, wasi_ptr_t out, wasi_size_t nsubscriptions, 
    wasi_ptr_t retptr0
) {
    wasi_errno_t status = WASI_ERRNO_SUCCESS;
    bool fault = false;

    // must have at least one entry waiting
    if (nsubscriptions == 0) {
        return WASI_ERRNO_INVAL;
    }

    // allocate the wait entries
    wait_entry_t* wait_entries = mem_calloc(sizeof(wait_entry_t), nsubscriptions);
    if (wait_entries == nullptr) return WASI_ERRNO_NOMEM;
    size_t wait_count = 0;

    // allocate the subscriptions, we want a copy so that we can't randomly 
    // fault when dealing with that data
    wasi_subscription_t* subscriptions = mem_calloc(sizeof(wasi_subscription_t), nsubscriptions);
    if (subscriptions == nullptr) return WASI_ERRNO_NOMEM;

    // copy the subscriptions in a single transaction
    if (!safe_copy(subscriptions, memory_base + in, sizeof(wasi_subscription_t) * nsubscriptions)) {
        return WASI_ERRNO_FAULT;
    }
    
    wasm_proc_t* proc = wasm_current_proc(state_base);

    // the events that we output
    wasi_size_t event_count = 0;
    wasi_event_t* events = memory_base + out;

    // 
    // Prepare all the waiters, this will potentially already find
    // ready entries that we will return right away instead of going 
    // to sleep
    //
    uint64_t min_deadline = -1;
    const wasi_subscription_t* deadline_sub;
    for (size_t i = 0; i < nsubscriptions; i++) {
        const wasi_subscription_t* sub = &subscriptions[i];

        //
        // Handle clock subscriptions, these basically gather 
        // into a min deadline for the wait
        //
        if (sub->tag == WASI_EVENTTYPE_CLOCK) {
            // TODO: for now we only support monotonic clock
            if (sub->clock.id != WASI_CLOCKID_MONOTONIC) {
                fault = wasi_event_set_errno(
                    &events[event_count++], sub, 
                    WASI_ERRNO_INVAL
                );

                if (fault)
                    goto cleanup;            
                else
                    continue;
            }

            // calculate the timeout for this clock
            uint64_t deadline;
            if (sub->clock.flags & WASI_SUBCLOCKFLAGS_SUBSCRIPTION_CLOCK_ABSTIME) {
                deadline = ns_to_tsc(sub->clock.timeout);
            } else {
                deadline = tsc_ns_deadline(sub->clock.timeout);
            }
            
            // if its smaller than the current deadline then use it
            // TODO: is this actually good enough? should we have a proper
            //       list of deadlines so we can go over them?
            if (min_deadline > deadline) {
                min_deadline = deadline;
                deadline_sub = sub;
            }
            continue;
        }

        // make sure it is a known value
        // TODO: should this fail completely?
        if (sub->tag != WASI_EVENTTYPE_FD_READ && sub->tag != WASI_EVENTTYPE_FD_WRITE) {
            status = WASI_ERRNO_INVAL;
            goto cleanup;
        }

        //
        // This is either fd_read/fd_write
        //

        // resolve the handle
        handle_t handle = handle_table_lookup(&proc->handles, sub->fd_read.file_descriptor);
        if (handle.object == nullptr) {
            fault = wasi_event_set_errno(
                &events[event_count++], sub, 
                WASI_ERRNO_BADF
            );

            if (fault)
                goto cleanup;            
            else
                continue;
        }
        object_t* object = handle.object;

        // make sure that the handle is waitable
        if ((handle.rights & RIGHT_WAIT) == 0) {
            fault = wasi_event_set_errno(
                &events[event_count++], sub, 
                WASI_ERRNO_NOTCAPABLE
            );
            
            object_put(object);

            if (fault)
                goto cleanup;            
            else
                continue;
        }

        // remember if this is read or write
        bool is_read = sub->tag == WASI_EVENTTYPE_FD_READ;
        
        // check if this is a wasi file, if it is perform the extra check
        wasi_file_t* file = wasi_file_from_object(object);
        if (file != nullptr) {
            wasi_rights_t right = is_read ? WASI_RIGHTS_FD_READ : WASI_RIGHTS_FD_WRITE;
            if (!wasi_file_is_capable(file, right | WASI_RIGHTS_POLL_FD_READWRITE)) {
                // the file is not capable for polling 
                // on the given poll type
                bool fault = wasi_event_set_errno(
                    &events[event_count++], sub, 
                    WASI_ERRNO_NOTCAPABLE
                );

                object_put(object);
                
                if (fault)
                    goto cleanup;            
                else
                    continue;
            }
        }

        // prepare for waiting
        wait_entry_t* entry = &wait_entries[wait_count];
        uint32_t signals = object_prepare_wait(
            object, 
            SIGNAL_CLOSED | SIGNAL_PEER_CLOSED | 
            (is_read ? SIGNAL_READABLE : SIGNAL_WRITABLE), 
            entry
        );

        // check if the signal was set
        if (signals != 0) {
            // the signal is already set, move it to the 
            // ready output right away
            bool fault = wasi_event_set_signal(
                &events[event_count++], 
                sub, 
                signals
            );

            // we need to close the object because it was 
            // not added to the list
            object_put(object);

            if (fault)
                goto cleanup;            
            else
                continue;
        }

        // remember the index
        wait_count++;
        entry->user_data = i;
    }

    // as long as nothing is ready, wait on stuff
    while (event_count == 0) {
        wait_status_t ws = sys_atomic_wait(wait_entries, wait_count, min_deadline);
        if (ws == WAIT_STATUS_OUT_OF_MEMORY) {
            status = WASI_ERRNO_NOMEM;
            goto cleanup;
        }

        // if we are past the deadline then add it as an event
        // TODO: should we maybe do a full pass and find all the 
        //       timeouts we got instead? 
        if (tsc_check_deadline(min_deadline)) {
            fault = wasi_event_set_errno(
                &events[event_count++], 
                deadline_sub, 
                WASI_ERRNO_SUCCESS
            );

            if (fault)
                goto cleanup;
        }

        // go over the wait entries and see if anything changed
        for (int i = 0; i < wait_count; i++) {
            wait_entry_t* entry = &wait_entries[i];
            wasi_subscription_t* sub = &subscriptions[entry->user_data];

            // check if the value has changed or we 
            // got a spurious wakeup
            uint32_t signals = atomic_load_acquire((_Atomic(uint32_t)*)entry->key);
            if (signals & entry->mask) {
                fault = wasi_event_set_signal(
                    &events[event_count++],
                    sub, 
                    signals
                );

                if (fault)
                    goto cleanup;

            } else {
                // update the value since we saw the new stuff
                entry->old = signals;
            }
        }
    }

cleanup:
    // remove the ref count of all the files we are waiting on
    for (int i = 0; i < wait_count; i++) {
        object_t* object = containerof(wait_entries[i].key, object_t, signals);
        object_put(object);
    }

    // free the wait entries
    mem_free(wait_entries);

    // copy out the value
    fault = !safe_copy(memory_base + retptr0, &event_count, sizeof(event_count));

    return fault ? WASI_ERRNO_FAULT : status;
}

//----------------------------------------------------------------------------------------------------------------------
// Processing
//----------------------------------------------------------------------------------------------------------------------

static noreturn void wasi_proc_exit(
    void* memory_base, void* state_base, 
    wasi_exitcode_t rval
) {
    wasm_proc_t* proc = wasm_current_proc(state_base);
    TRACE("wasi_proc_exit: %d", rval);

    // TODO: tell all threads to die

    // exit this thread
    wasm_thread_exit(state_base);
}

static wasi_errno_t wasi_sched_yield(
    void* memory_base, void* state_base
) {
    sys_thread_yield();
    return WASI_ERRNO_SUCCESS;
}

//----------------------------------------------------------------------------------------------------------------------
// The actual function definitions
//----------------------------------------------------------------------------------------------------------------------

static const runtime_function_t m_wasi_functions[] = {
    RUNTIME_FUNCTION(wasi, clock_time_get, I32, I32, I64, I32),

    RUNTIME_FUNCTION(wasi, fd_close, I32, I32),
    RUNTIME_FUNCTION(wasi, fd_fdstat_get, I32, I32, I32),
    RUNTIME_FUNCTION(wasi, fd_seek, I32, I32, I64, I32, I32),
    RUNTIME_FUNCTION(wasi, fd_write, I32, I32, I32, I32, I32),

    RUNTIME_FUNCTION(wasi, poll_oneoff, I32, I32, I32, I32, I32),

    RUNTIME_FUNCTION(wasi, proc_exit, INVALID, I32),
    RUNTIME_FUNCTION(wasi, sched_yield, I32),
};

void* wasi_resolve_import(const char* name, wasm_type_t* type) {
    return runtime_resolve_function(
        m_wasi_functions, 
        ARRAY_LENGTH(m_wasi_functions), 
        name, type
    );
}
