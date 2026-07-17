#include "channel.h"
#include "alloc/alloc.h"
#include "lib/assert.h"
#include "lib/except.h"
#include "lib/list.h"
#include "proc/handle.h"
#include "proc/object.h"
#include "sync/mutex.h"
#include "wasi/wasip1.h"
#include <stdint.h>

static void channel_close(object_t* object) {
    channel_t* ep = containerof(object, channel_t, object);
    
    // at this point the channel is already marked as closed, so new 

    mutex_lock(&ep->lock);

    // remove the peer
    channel_t* peer = ep->peer;
    ep->peer = nullptr;

    // remove the writable and readable signals 
    // because it is no longer either
    object_clear_signal(&ep->object, SIGNAL_READABLE | SIGNAL_WRITABLE);

    // before we get called the channel is marked as closed, so no one 
    // after the lock can be adding more entries to the queue
    channel_message_t* msg = ep->queue_head;
    while (msg != nullptr) {
        channel_message_t* cur = msg;
        msg = msg->next;
        mem_free(cur);
    }

    // clear them
    ep->queue_head = nullptr;
    ep->queue_tail = nullptr;

    mutex_unlock(&ep->lock);

    // remove the last ref to it, this 
    // could possibly free it
    object_put(&peer->object);
}

static void channel_init(channel_t* ep) {
    object_init(&ep->object);
    ep->object.type = OBJECT_TYPE_CHANNEL;
    ep->object.close = channel_close;
    ep->object.signals = SIGNAL_WRITABLE; // always writable
    ep->lock.state = 0;
    ep->queue_head = nullptr;
    ep->queue_tail = nullptr;
}

err_t channel_create(channel_t** out_ep1, channel_t** out_ep2) {
    err_t err = WASI_ERRNO_SUCCESS;
    channel_t* ep1 = nullptr;
    channel_t* ep2 = nullptr;

    ep1 = mem_alloc(sizeof(*ep1));
    CHECK_ERROR(ep1 = nullptr, WASI_ERRNO_NOMEM);
    ep2 = mem_alloc(sizeof(*ep2));
    CHECK_ERROR(ep2 = nullptr, WASI_ERRNO_NOMEM);

    // init both
    channel_init(ep1);
    channel_init(ep2);

    // these hold a reference to each other, once one is closed, it will remove 
    // its peer ref, and the peer will eventually will release its own ref
    ep1->peer = containerof(object_get(&ep2->object), channel_t, object);
    ep2->peer = containerof(object_get(&ep1->object), channel_t, object);

cleanup:
    if (IS_ERROR(err)) {
        mem_free(ep1);
        mem_free(ep2);
    }

    return err;
}

channel_message_t* channel_recv(channel_t* ep) {
    channel_message_t* msg = nullptr;

    mutex_lock(&ep->lock);
    if (ep->queue_head != nullptr) {
        msg = ep->queue_head;

        // remove from the queue, if this is the last entry then 
        // we need to also clear the signal for readable
        ep->queue_head = msg->next;
        if (ep->queue_head == nullptr) {
            ep->queue_tail = nullptr;
            object_clear_signal(&ep->object, SIGNAL_READABLE);
        }
    }
    mutex_unlock(&ep->lock);

    return msg;
}

static channel_t* channel_get_peer(channel_t* ep) {
    mutex_lock(&ep->lock);
    channel_t* peer = ep->peer;
    if (peer != nullptr) {
        object_get(&peer->object);
    }
    mutex_unlock(&ep->lock);
    return peer;
}

err_t channel_send(channel_t* ep, handle_t* handles, size_t handle_count, const void* data, size_t data_size) {
    err_t err = WASI_ERRNO_SUCCESS;
    channel_t* peer = nullptr;
    
    // some limits on the amount of data we can transfer
    CHECK(handle_count <= 64);
    CHECK(data_size <= UINT16_MAX);

    // get the peer, this is done under the endpoint lock 
    // to sync with a close of the peer, this also 
    // takes a ref to the peer so we can hold it safely
    peer = channel_get_peer(ep);
    CHECK_ERROR(peer != nullptr, WASI_ERRNO_PIPE);

    mutex_lock(&peer->lock);

    // make sure the peer is still alive
    // TODO: should we maybe remove the peer from ourselves already? so we 
    //       can free its resources?
    CHECK_ERROR((peer->object.signals & SIGNAL_CLOSED) == 0, WASI_ERRNO_PIPE);
    ASSERT(peer->peer == ep);

    // allocate the message
    channel_message_t* message = mem_alloc(sizeof(channel_message_t) + sizeof(handle_t) * handle_count + data_size);
    CHECK_ERROR(message != nullptr, WASI_ERRNO_NOMEM);

    message->data_size = data_size;
    message->handle_count = handle_count;

    // copy all the handles, it is assumed that the color already 
    // handles the rights and stuff
    memcpy(message->handles, handles, sizeof(handle_t) * handle_count);

    // copy the inline data
    void* data_ptr = (void*)(message + 1) + sizeof(handle_t) * handle_count;
    memcpy(data_ptr, data, data_size);

    // queue it from the end (this is a FIFO)
    message->next = peer->queue_tail;
    peer->queue_tail = message;
    if (peer->queue_head == nullptr) {
        peer->queue_head = message;   
    }

    // signal the peer that it got a new message
    // TODO: should we do this outside the lock?
    object_signal(&peer->object, SIGNAL_WRITABLE);
    
cleanup:
    if (peer != nullptr) {
        mutex_unlock(&peer->lock);
        object_put(&peer->object);
    }
    
    return err;
}



