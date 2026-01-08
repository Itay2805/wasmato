#pragma once

#include <stddef.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The general purpose allocator's virtual address space management
// Used to provide sbrk/mmap like functionality to the allocator
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Performs a simple sbrk like expansion of the heap, will return NULL
 * if there is not enough room
 *
 * @param size  [IN] The size in bytes
 * @return Pointer to the new top of the sbrk region
 */
void* valloc_expand(size_t size);

/**
 * Performs a simple anonymous mmap like allocation, it
 * will always be page aligned
 *
 * @param size  [IN] The size in bytes
 * @return Pointer to the allocated region
 */
void* valloc_alloc(size_t size);

/**
 * Free an allocated region, does not support partial frees
 *
 * @param ptr   [IN] The base of the area to free
 * @param size  [IN] The size of the area that was allocated
 */
void valloc_free(void* ptr, size_t size);
