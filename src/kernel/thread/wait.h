#pragma once

#include <stddef.h>
#include <stdint.h>

#include "lib/defs.h"
#include "uapi/wait.h"

INIT_CODE void init_atomic_wait(void);

/**
 * Atomically checks that every entry's `key` still contains its expected `old`
 * value and then parks the current thread until it is either woken by an
 * `atomic_notify` on any of the keys or the deadline expires.
 *
 * Each entry's value may be either 32-bit or 64-bit, as indicated by its
 * `key_size`. At least one and at most 64 entries may be supplied.
 *
 * If any entry's `key` no longer holds its expected `old` value the function
 * does not park and returns `false` immediately.
 *
 * Typical usage patterns involving memory reclamation (*by unrelated code*)
 * may cause this function to return spuriously, without any `key` having
 * changed, without `atomic_notify` having been invoked and without the
 * deadline having expired. Callers must therefore invoke this function in a
 * loop until a suitable value is observed in one of the keys.
 *
 * @param entries   [IN] The array of keys to wait on, each with its expected value and size
 * @param count     [IN] The number of entries, between 1 and 64
 * @param deadline  [IN] The deadline to wait for, 0 for no deadline
 * @returns true if we went to sleep, false if an entry failed to compare against its old value
 */
bool atomic_wait(wait_entry_t* entries, size_t count, uint64_t deadline);

/**
 * Wakes a number of threads currently waiting on `key` via `atomic_wait`.
 *
 * @param key       [IN] The key to wake
 * @param count     [IN] The number of threads to wake, 0 for all
 * @return The number of threads woken
 */
size_t atomic_notify(void* key, size_t count);
