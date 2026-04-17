#pragma once

#include <stdint.h>

#include "lib/except.h"

/**
 * Initialize the early acpi subsystem, should just be enough for
 * doing whatever we need to do
 */
err_t INIT_CODE init_acpi_tables(void);

/**
 * Get the ACPI PM Timer tick value
 */
uint32_t INIT_CODE acpi_get_timer_tick(void);
