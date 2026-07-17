#pragma once

#include "lib/except.h"
#include "proc/handle.h"
#include "proc/object.h"
#include "sync/mutex.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct channel_message {
    /** 
     * The next message in the channel
     */
    struct channel_message* next;

    /**
     * The amount of handles we transfer
     */
    uint8_t handle_count;

    /** 
     * The amount of inline data
     */
    uint16_t data_size;

    /**
     * The handles we transfer
     */
    handle_t handles[];

    // after the handles we have more data we transfer
} channel_message_t;

typedef struct channel {
    object_t object;

    /**
     * The queue of messages we transfer
     */
    channel_message_t* queue_head;
    channel_message_t* queue_tail;

    /**
     * The peer of this channel
     */
    struct channel* peer;

    /** 
     * The lock to protect the queue
     */
    mutex_t lock;
} channel_t;

/** 
 * Create a channel with two endpoints
 */
err_t channel_create(channel_t** ep1, channel_t** ep2);

/**
 * Recv data from a channel, returns null if the channel is empty
 */
channel_message_t* channel_recv(channel_t* ep);

/**
 * Send data over the channel, this will buffer, returns an error if out of memory
 */
err_t channel_send(channel_t* ep, handle_t* handles, size_t handle_count, const void* data, size_t data_size);
