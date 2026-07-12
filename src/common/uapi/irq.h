#pragma once

#include <stdint.h>

#include "lib/atomic.h"

/** 
 * The value inside of the interrupt waiter while it is parked, should 
 * be set before going to sleep
 */
#define IRQ_WAITER_PARKED ((uint32_t)1)

/**
 * The value inside the interrupt waiter once the thread should wake up
 * to get the interrupt
 */
#define IRQ_WAITER_WAKEUP ((uint32_t)0)

/**
 * The IRQ is dead, if we happen to still be waiting on it
 * then we should wakeup the waiter properly
 */
#define IRQ_WAITER_DEAD ((uint32_t)-1)

typedef _Atomic(uint32_t) irq_waiter_t;
