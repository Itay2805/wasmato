#pragma once

#include "lib/defs.h"
#include "user/object.h"
#include <stdint.h>

INIT_CODE void init_handle_table(void);

uint64_t handle_register(void* object);

/*
 * Resolve a handle to its object, taking a fresh reference.
 *
 * On success, the caller owns a new reference and must drop it with
 * kernel_object_put() when done. Returns NULL if the handle is unknown,
 * already closed, or being closed concurrently.
 */
void* handle_lookup(uint64_t handle);

void handle_close(uint64_t handle);
