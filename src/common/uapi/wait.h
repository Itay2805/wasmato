#pragma once

#include <stdint.h>

typedef enum wait_key_size : uint32_t {
	WAIT_KEY_UINT32,
	WAIT_KEY_UINT64,
} wait_key_size_t;

typedef enum wait_status {
    WAIT_STATUS_SUCCESS,
    WAIT_STATUS_NOT_EQUAL,
    WAIT_STATUS_OUT_OF_MEMORY,
} wait_status_t;

typedef struct wait_entry {
    void* key;
    uint64_t old;
    uint64_t mask;
    wait_key_size_t key_size;
    uint32_t user_data;
} wait_entry_t;

typedef struct wake_params {
    void* key;
    uint64_t mask;
    wait_key_size_t key_size;
} wake_params_t;
