#pragma once

#include "error.h"
#include <__header_time.h>
#include <stdbool.h>
#include <stdint.h>

#include <errno.h>
#include <pthread.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>

#define NANOSECONDS_PER_SECOND 1000000000ull

static inline uint64_t get_nanosecond_timer(void) {
    struct timespec ts = {};
    if (clock_gettime(CLOCK_MONOTONIC, &ts))
        error("clock_gettime failed");
    return ts.tv_sec * NANOSECONDS_PER_SECOND + ts.tv_nsec;
}

static inline void* get_thread_id(void) {
    return pthread_self();
}

static inline void millisecond_sleep(uint64_t milliseconds) {
    if (usleep(milliseconds * 1000))
        error("usleep failed");
}

static inline void mutex_init(pthread_mutex_t* mutex) {
    if (pthread_mutex_init(mutex, NULL))
        error("pthread_mutex_init failed");
}

static inline void mutex_free(pthread_mutex_t* mutex) {
    if (pthread_mutex_destroy(mutex))
        error("pthread_mutex_destroy failed");
}

static inline bool mutex_try_lock(pthread_mutex_t* mutex){
    int err = pthread_mutex_trylock(mutex);

    if (err == 0)
        return true;
    if (err != EBUSY)
        error("pthread_mutex_trylock failed");
    return false;
}

static inline void mutex_lock(pthread_mutex_t* mutex) {
    if (pthread_mutex_lock(mutex))
        error("pthread_mutex_lock failed");
}

static inline bool mutex_lock_timeout(pthread_mutex_t* mutex, uint64_t timeout_ns) {
    uint64_t end = get_nanosecond_timer() + timeout_ns;

    do {
        if (mutex_try_lock(mutex)) {
            return true;
        }
        millisecond_sleep(1);
    } while (get_nanosecond_timer() < end);

    return false;
}

static inline void mutex_unlock(pthread_mutex_t* mutex) {
    if (pthread_mutex_unlock(mutex))
        error("pthread_mutex_unlock failed");
}

static inline void condvar_init(pthread_cond_t* var) {
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr))
        error("pthread_condattr_init failed");

    if (pthread_condattr_setclock(&attr, CLOCK_MONOTONIC))
        error("pthread_condattr_setclock failed");

    if (pthread_cond_init(var, &attr))
        error("pthread_cond_init failed");

    pthread_condattr_destroy(&attr);
}

static inline void condvar_free(pthread_cond_t* var) {
    if (pthread_cond_destroy(var))
        error("pthread_cond_destroy failed");
}

typedef bool (*condvar_pred_t)(void *ctx);

static inline void condvar_wait(
    pthread_cond_t* var, pthread_mutex_t* mutex, 
    condvar_pred_t pred, void *ctx
) {
    while (!pred(ctx))
        if (pthread_cond_wait(var, mutex))
            error("pthread_cond_wait failed");
}

static inline bool condvar_wait_timeout(
    pthread_cond_t* var, pthread_mutex_t* mutex, 
    condvar_pred_t pred, void *ctx,
    uint64_t timeout_ns
) {
    struct timespec spec = {};
    if (clock_gettime(CLOCK_MONOTONIC, &spec))
        error("clock_gettime failed");

    spec.tv_nsec += timeout_ns;
    spec.tv_sec += spec.tv_nsec / NANOSECONDS_PER_SECOND;
    spec.tv_nsec %= NANOSECONDS_PER_SECOND;

    while (!pred(ctx)) {
        int err = pthread_cond_timedwait(var, mutex, &spec);

        if (err == 0)
            continue;
        if (err != ETIMEDOUT)
            error("pthread_cond_clockwait failed");
        return false;
    }

    return true;
}

static inline void condvar_signal(pthread_cond_t* var) {
    if (pthread_cond_signal(var))
        error("pthread_cond_signal failed");
}
