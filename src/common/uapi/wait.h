#pragma once

#include <stdint.h>

/**
 * The max amount of entries that can be given 
 * to the kernel for waiting
 */
#define MAX_WAIT_ENTRIES 64

typedef enum wait_key_size {
	WAIT_KEY_UINT32,
	WAIT_KEY_UINT64,
} wait_key_size_t;

typedef struct wait_entry {
    void* key;
    uint64_t old;
    wait_key_size_t key_size;
} wait_entry_t;
