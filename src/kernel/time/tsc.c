#include "tsc.h"
#include "lib/tsc.h"

#include <cpuid.h>

#include "acpi/acpi.h"
#include "arch/intrin.h"
#include "lib/defs.h"
#include "lib/list.h"

/**
 * The calculated TSC resolution
 */
uint64_t g_tsc_freq_hz = 0;

/**
 * Quick calibration using the ACPI timer, this is enough to get started with timers
 * and anything that requires delays, but it can be quite off from the real thing
 */
INIT_CODE static uint64_t quick_acpi_timer_calibrate(void) {
    uint32_t ticks = acpi_get_timer_tick() + 363;
    uint64_t start_tsc = get_tsc();
    while (((ticks - acpi_get_timer_tick()) & BIT23) == 0) {
        cpu_relax();
    }
    uint64_t end_tsc = get_tsc();
    return (end_tsc - start_tsc) * 9861;
}

INIT_CODE err_t init_tsc_early(void) {
    err_t err = NO_ERROR;

    g_tsc_freq_hz = quick_acpi_timer_calibrate();
    CHECK(g_tsc_freq_hz != 0);
    TRACE("timer: Fast TSC calibration using ACPI Timer %lu.%03lu MHz",
        g_tsc_freq_hz / 1000000, (g_tsc_freq_hz / 1000) % 1000);

    // TODO: what should we do about refinement

cleanup:
    return err;
}

INIT_CODE bool tsc_deadline_is_supported(void) {
    uint32_t a, b, c, d;
    __cpuid(1, a, b, c, d);
    return c & bit_TSCDeadline;
}

void tsc_timer_set_deadline(uint64_t tsc_deadline) {
    __wrmsr(MSR_IA32_TSC_DEADLINE, tsc_deadline);
}

void tsc_timer_clear(void) {
    __wrmsr(MSR_IA32_TSC_DEADLINE, 0);
}
