#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lib/except.h"

/**
 * Initialize the timer subsystem, calculating the frequency of the TSC so it can be used for time keeping
 */
err_t INIT_CODE init_tsc_early(void);

/**
 * Returns true if the CPU supports TSC deadline
 */
bool tsc_deadline_is_supported(void);

void tsc_timer_set_deadline(uint64_t tsc_deadline);
void tsc_timer_clear(void);
