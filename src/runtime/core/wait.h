#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum wait_key_size {
    WAIT_KEY_UINT32,
    WAIT_KEY_UINT64,
} wait_key_size_t;

void init_atomic_wait(void);

/**
 * Atomically checks that `key` contains the value `old` and then parks the
 * current thread until it is either woken by `atomic_notify` or the deadline
 * expires.
 *
 * The value pointed to by `key` may be either 32-bit or 64-bit, as indicated
 * by `size`.
 *
 * Typical usage patterns involving memory reclamation (*by unrelated code*)
 * may cause this function to return spuriously, without `key` having changed,
 * without `atomic_notify` having been invoked and without the deadline having
 * expired. Callers must therefore invoke this function in a loop until a
 * suitable value is observed in `key`.
 *
 * @param key       [IN] The pointer of the key
 * @param size      [IN] The size of the key
 * @param old       [IN] The current value we expect
 * @param deadline  [IN] The deadline to wait for, 0 for no deadline
 */
void atomic_wait(void* key, wait_key_size_t size, uint64_t old, uint64_t deadline);

/**
 * Wakes a number of threads currently waiting on `key` via `atomic_wait`.
 *
 * @param key       [IN] The key to wake
 * @param count     [IN] The number of threads to wake, 0 for all
 * @return The number of threads woken
 */
size_t atomic_notify(void* key, size_t count);
