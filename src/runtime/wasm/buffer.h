#pragma once

#include "lib/except.h"


typedef struct buffer {
    void* data;
    size_t len;
} buffer_t;

static inline buffer_t init_buffer(void* data, size_t len) {
    return (buffer_t){
        .data = data,
        .len = len,
    };
}

err_t buffer_pull_u32(buffer_t* buffer, uint32_t* value);
err_t buffer_pull_u64(buffer_t* buffer, uint64_t* value);
err_t buffer_pull_i32(buffer_t* buffer, int32_t* value);
err_t buffer_pull_i64(buffer_t* buffer, int64_t* value);

err_t buffer_pull_name(buffer_t* buffer, buffer_t* name);

static inline void* buffer_pull(buffer_t* buffer, size_t len) {
    if (buffer->len < len) {
        return nullptr;
    }
    void* ptr = buffer->data;
    buffer->data += len;
    buffer->len -= len;
    return ptr;
}

#define BUFFER_PULL(type, buffer) \
    ({ \
        type* data__ = buffer_pull(buffer, sizeof(type)); \
        CHECK(data__ != nullptr); \
        *data__; \
    })

#define BUFFER_PULL_U32(buffer) \
    ({ \
        uint32_t value__ = 0; \
        RETHROW(buffer_pull_u32(buffer, &value__)); \
        value__; \
    })

#define BUFFER_PULL_U64(buffer) \
    ({ \
        uint64_t value__ = 0; \
        RETHROW(buffer_pull_u64(buffer, &value__)); \
        value__; \
    })

#define BUFFER_PULL_I32(buffer) \
    ({ \
        int32_t value__ = 0; \
        RETHROW(buffer_pull_i32(buffer, &value__)); \
        value__; \
    })

#define BUFFER_PULL_I64(buffer) \
    ({ \
        int64_t value__ = 0; \
        RETHROW(buffer_pull_i64(buffer, &value__)); \
        value__; \
    })
