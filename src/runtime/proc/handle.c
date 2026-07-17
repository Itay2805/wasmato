#include "handle.h"
#include "alloc/alloc.h"
#include "lib/assert.h"
#include "lib/defs.h"
#include "lib/except.h"
#include "sync/mutex.h"
#include "object.h"
#include <stddef.h>
#include <stdint.h>

static bool handle_table_grow(handle_table_t* table, size_t count) {
    ASSERT((count % 64) == 0);

    // attempt to increase the capacity, if we overflow then we are 
    // out of space for new handles
    uint32_t new_capacity;
    if (
        __builtin_add_overflow(table->capacity, count, &new_capacity) ||
        new_capacity > INT32_MAX
    ) {
        return false;
    }

    // capacity is in uint32, so we can multiply it by the size of object without overflow,
    // if we failed then we are out of memory for new objects
    handle_t* new_array = mem_realloc(table->array, sizeof(*table->array) * new_capacity);
    if (new_array == nullptr) {
        return false;
    }

    // clear the objects and remember it
    // NOTE: if we fail to realloc the open array we will still have this 
    //       new array, we could probably do a realloc to a smaller size 
    //       but idk if this even does anything or if its worth it
    for (int i = table->capacity; i < new_capacity; i++) {
        new_array[i].object = nullptr;
        new_array[i].rights = 0;
    }
    table->array = new_array;

    // now reallocate the open array
    uint64_t* new_open = mem_realloc(table->open, sizeof(*table->open) * (new_capacity / 64));
    if (new_open == nullptr) {
        return false;
    }

    // clear and remember
    for (int i = table->capacity / 64; i < new_capacity / 64; i++) {
        new_open[i] = 0;
    }
    table->open = new_open;

    // and mark the new capacity
    table->capacity = new_capacity;

    return true;
}

int handle_table_allocate(handle_table_t* table, object_t* object, rights_t rights) {
    mutex_lock(&table->lock);

    // search for an unused entry
    int handle = -1;
    for (size_t i = 0; i < table->capacity / 64; i++) {
        uint64_t inv = ~table->open[i];
        if (inv) {
            handle = i * 64 + (size_t)__builtin_ctzll(inv);
            break;
        }
    }

    if (handle < 0) {
        // the table is full, increase its size
        int potential_handle = table->capacity;
        
        // attempt to grow the table, fail if we can't
        if (!handle_table_grow(table, 64)) {
            goto cleanup;
        }

        // we done did it
        handle = potential_handle;
    }

    // we got a valid handle!
    ASSERT(handle >= 0);

    // install the object
    table->open[handle / 64] |= 1 << (handle % 64);
    table->array[handle].object = object_handle_get(object);
    table->array[handle].rights = rights;

cleanup:
    mutex_unlock(&table->lock);
    return handle;
}

bool handle_table_install(handle_table_t* table, object_t* object, rights_t rights, int handle) {
    mutex_lock(&table->lock);

    // check if we need to grow the table to fit the new fd
    size_t capacity = table->capacity;
    if (handle >= capacity) {
        size_t new_capacity = ALIGN_UP((size_t)handle + 1, 64);
        if (!handle_table_grow(table, new_capacity - capacity)) {
            mutex_unlock(&table->lock);
            return false;
        }
    }

    // install the object
    table->open[handle / 64] |= 1ull << (handle % 64);
    table->array[handle].object = object_handle_get(object);
    table->array[handle].rights = rights;
    
    mutex_unlock(&table->lock);
    return true;
}

handle_t handle_table_lookup(handle_table_t* table, int handle) {
    // TODO: we can do this lockless I think because the table only ever grows
    //       or with RCU primitive we could probably do something even stronger
    //       but for now this will do fine
    // TODO: maybe at least replace with rwlock?

    mutex_lock(&table->lock);

    handle_t out_handle = {};

    // only get the object if the handle is within bounds
    if (handle < table->capacity) {
        out_handle = table->array[handle];

        // if we got an object increase its ref-count
        if (out_handle.object != nullptr) {
            object_get(out_handle.object);
        }
    }

    mutex_unlock(&table->lock);
    return out_handle;
}

bool handle_table_close(handle_table_t* table, int handle) {
    bool success = false;
    mutex_lock(&table->lock);

    handle_t out_handle = {};

    // only get the object if the handle is within bounds
    if (handle < table->capacity) {
        out_handle = table->array[handle];

        // if we got an object then remove it from the table
        if (out_handle.object != nullptr) {
            table->open[handle / 64] &= ~(1ull << (handle % 64));
            table->array[handle].object = nullptr;
            table->array[handle].rights = 0;

            // no longer a handle
            object_handle_put(out_handle.object);

            success = true;
        }
    }

    mutex_unlock(&table->lock);
    return success;
}
