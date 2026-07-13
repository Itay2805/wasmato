#include "handle.h"
#include "lib/assert.h"
#include "lib/atomic.h"
#include "lib/defs.h"

#include <stdatomic.h>
#include <stdint.h>

#include "lib/log.h"
#include "lib/string.h"
#include "lib/random.h"
#include "lib/siphash.h"
#include "sync/spinlock.h"
#include "uapi/syscall.h"
#include "user/object.h"

typedef struct handle_slot {
    /**
     * The expected mac
     */
    uint64_t mac;

    /**
     * The object assigned to the slot
     */
    void* object;
} handle_slot_t;

#define HANDLE_INDEX_BITS   (16)
#define HANDLE_MAC_BITS     (64 - HANDLE_INDEX_BITS)

#define HANDLE_MAC_MASK     ((1ull << HANDLE_MAC_BITS) - 1)
#define HANDLE_INDEX_MASK   ((1ull << HANDLE_INDEX_BITS) - 1)

/**
 * The key for the siphashsiphash.h
 */
static uint8_t m_handle_key[16];

/**
 * Just a global nonce-generator
 */
static uint64_t m_handle_nonce = 0;

/**
 * Our handle table
 */
static handle_slot_t m_handles[INVALID_HANDLE] = {};

/**
 * Lock to protect the handle table
 */
static spinlock_t m_handle_lock = SPINLOCK_INIT;

INIT_CODE void init_handle_table(void) {
    boot_random_fill(m_handle_key, sizeof(m_handle_key));
}

static uint64_t handle_mac(uint32_t index, uint64_t counter) {
    uint8_t in[sizeof(counter) + sizeof(index)];
    memcpy(in, &counter, sizeof(counter));
    memcpy(in + sizeof(counter), &index, sizeof(index));

    uint8_t out[sizeof(uint64_t)];
    siphash(in, sizeof(in), m_handle_key, out, sizeof(out));

    uint64_t mac;
    memcpy(&mac, out, sizeof(mac));
    return mac & HANDLE_MAC_MASK;
}

uint64_t handle_register(void* object) {
    uint64_t handle = INVALID_HANDLE;

    spinlock_acquire(&m_handle_lock);
    
    for (size_t i = 0; i < ARRAY_LENGTH(m_handles); i++) {
        if (m_handles[i].object != NULL) {
            continue;
        }

        uint64_t mac = handle_mac(i, m_handle_nonce++);
        m_handles[i].mac = mac;
        m_handles[i].object = object;
        handle = i | (mac << HANDLE_INDEX_BITS);
        break;
    }

    spinlock_release(&m_handle_lock);

    // Table full
    return handle;
}

void* handle_lookup(uint64_t handle) {
    spinlock_acquire(&m_handle_lock);
    
    uint32_t index = handle & HANDLE_INDEX_MASK;
    uint64_t mac = handle >> HANDLE_INDEX_BITS;
    
    handle_slot_t* slot = &m_handles[index];
    ASSERT(slot->object != NULL);
    ASSERT(slot->mac == mac);
    
    void* object = kernel_object_get(slot->object);
    
    spinlock_release(&m_handle_lock);

    return object;
}

void handle_close(uint64_t handle) {
    spinlock_acquire(&m_handle_lock);
    
    uint32_t index = handle & HANDLE_INDEX_MASK;
    uint32_t mac = handle >> HANDLE_MAC_BITS;
    
    handle_slot_t* slot = &m_handles[index];
    ASSERT(slot->object != NULL);
    ASSERT(slot->mac == mac);
    
    kernel_object_put(slot->object);

    slot->mac = 0;
    slot->object = NULL;
    
    spinlock_release(&m_handle_lock);
}
