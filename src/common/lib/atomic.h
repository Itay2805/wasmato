#pragma once

#include <stdatomic.h>

#define atomic_fence_acquire() atomic_thread_fence(memory_order_acquire)
#define atomic_fence_release() atomic_thread_fence(memory_order_release)

#define atomic_store_relaxed(ptr, value)                                       \
    atomic_store_explicit(ptr, value, memory_order_relaxed)

#define atomic_store_release(ptr, value)                                       \
    atomic_store_explicit(ptr, value, memory_order_release)

#define atomic_load_relaxed(ptr) atomic_load_explicit(ptr, memory_order_relaxed)
#define atomic_load_acquire(ptr) atomic_load_explicit(ptr, memory_order_acquire)

#define atomic_exchange_acquire(ptr, value)                                    \
    atomic_exchange_explicit(ptr, value, memory_order_acquire)

#define atomic_exchange_release(ptr, value)                                    \
    atomic_exchange_explicit(ptr, value, memory_order_release)

#define atomic_compare_exchange_weak_relaxed(ptr, expected, desired)           \
    atomic_compare_exchange_weak_explicit(                                     \
        ptr, expected, desired, memory_order_relaxed, memory_order_relaxed)

#define atomic_compare_exchange_weak_acquire_relaxed(ptr, expected, desired)   \
    atomic_compare_exchange_weak_explicit(                                     \
        ptr, expected, desired, memory_order_acquire, memory_order_acquire)

#define atomic_compare_exchange_strong_relaxed(ptr, expected, desired)         \
    atomic_compare_exchange_strong_explicit(                                   \
        ptr, expected, desired, memory_order_relaxed, memory_order_relaxed)

#define atomic_compare_exchange_strong_release(ptr, expected, desired)         \
    atomic_compare_exchange_strong_explicit(                                   \
        ptr, expected, desired, memory_order_release, memory_order_relaxed)

#define atomic_compare_exchange_strong_acquire(ptr, expected, desired)         \
    atomic_compare_exchange_strong_explicit(                                   \
        ptr, expected, desired, memory_order_acquire, memory_order_acquire)
