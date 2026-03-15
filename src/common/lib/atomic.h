#pragma once

#include <stdatomic.h>

#define atomic_store_relaxed(ptr, value) atomic_store_explicit(ptr, value, memory_order_relaxed)
#define atomic_store_release(ptr, value) atomic_store_explicit(ptr, value, memory_order_release)

#define atomic_load_relaxed(ptr) atomic_load_explicit(ptr, memory_order_relaxed)
#define atomic_load_acquire(ptr) atomic_load_explicit(ptr, memory_order_acquire)
