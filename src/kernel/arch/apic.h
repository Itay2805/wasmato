#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lib/except.h"

/**
 * Initialize the APIC globally
 */
err_t init_lapic(void);

/**
 * Initialize the APIC per core
 */
err_t init_lapic_per_core(void);

/**
 * Request an EOI signal to be sent
 */
void lapic_eoi(void);

/**
 * Recalibrate the lapic timer
 */
void lapic_timer_recalibrate(void);

/**
 * Set the lapic deadline to the given deadline
 */
void lapic_timer_set_deadline(uint64_t tsc_deadline);

/**
 * Clear the lapic timer
 */
void lapic_timer_clear(void);
