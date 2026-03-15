#pragma once

#include "lib/list.h"
#include "sync/spinlock.h"

typedef enum wait_key_size {
    WAIT_KEY_UINT32,
    WAIT_KEY_UINT64,
} wait_key_size_t;

/**
 * Waits on an memory location to change value, the key can either be 32bit
 * or 64bit.
 *
 * @param key       [IN] The pointer of the key
 * @param size      [IN] The size of the key
 * @param old       [IN] The current value we expect
 * @param deadline  [IN] The deadline to wait for, 0 for no deadline
 * @returns true on wakeup, false on timeout
 */
bool atomic_wait(void* key, wait_key_size_t size, uint64_t old, uint64_t deadline);

/**
 * Wakeup up to the given amount of threads from the wait queue
 *
 * @param key       [IN] The key to wake
 * @param count     [IN] The amount of threads to wake, 0 for all
 */
size_t atomic_notify(void* key, size_t count);
