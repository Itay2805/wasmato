#include "tsc.h"

#include <stdbool.h>
#include <stddef.h>

#include <cpuid.h>

#include "timer.h"
#include "acpi/acpi.h"
#include "arch/apic.h"
#include "arch/intrin.h"
#include "lib/defs.h"
#include "lib/list.h"
#include "../mem/kernel/alloc.h"
#include "sync/spinlock.h"

typedef struct tsc_refine_ctx {
    timer_t timer;
    uint64_t ref_start;
    uint64_t tsc_start;
} tsc_refine_ctx_t;

/**
 * The calculated TSC resolution
 */
uint64_t g_tsc_freq_hz = 0;

/**
 * Quick calibration using the ACPI timer, this is enough to get started with timers
 * and anything that requires delays, but it can be quite off from the real thing
 */
static uint64_t quick_acpi_timer_calibrate(void) {
    bool irq = irq_save();
    uint32_t ticks = acpi_get_timer_tick() + 363;
    uint64_t start_tsc = get_tsc();
    while (((ticks - acpi_get_timer_tick()) & BIT23) == 0) {
        cpu_relax();
    }
    uint64_t end_tsc = get_tsc();
    irq_restore(irq);
    return (end_tsc - start_tsc) * 9861;
}

/**
 * Read both the tsc and the ref, this ensures that we don't have a
 * deviation that is too big which could come from an interrupt or
 * nmi or god knows what
 */
static uint64_t tsc_read_refs(uint64_t* p) {
    uint64_t thresh = (g_tsc_freq_hz / 1000) >> 5;
    for (int i = 0; i < 5; i++) {
        uint64_t t1 = get_tsc();
        *p = acpi_get_timer_tick();
        uint64_t t2 = get_tsc();
        if ((t2 - t1) < thresh) {
            return t2;
        }
    }
    return UINT64_MAX;
}

/**
 * Given two ACPI timer refs, and the delta between the tsc from the reading
 * of the first and second ref, calculate the tsc frequency
 */
static uint64_t tsc_calc_acpi_timer_ref(uint64_t deltatsc, uint64_t pm1, uint64_t pm2) {
    if (pm1 == 0 || pm2 == 0) {
        return UINT64_MAX;
    }

    if (pm2 < pm1) {
        pm2 += 1 << 24;
    }
    pm2 -= pm1;
    return deltatsc / ((pm2 * 1000000000LL) / 3579545);
}

/**
 * Runs after a second from reading the first refs, used to have a more accurate
 * tsc frequency value which we can use for time keeping in the long run
 */
static void tsc_refine_callback(timer_t* timer) {
    // read the stop tsc and reference
    uint64_t ref_stop;
    uint64_t tsc_stop = tsc_read_refs(&ref_stop);

    tsc_refine_ctx_t* ctx = containerof(timer, tsc_refine_ctx_t, timer);
    ASSERT(ctx->ref_start != ref_stop);

    if (tsc_stop == UINT64_MAX) {
        // sampling was disturbed, try again
        WARN("tsc: refinement was disturbed");
        ctx->tsc_start = tsc_read_refs(&ctx->ref_start);
        timer_set(&ctx->timer, tsc_refine_callback, tsc_ms_deadline(1000));
        return;
    }

    // update the frequency
    uint64_t delta = tsc_stop - ctx->tsc_start;
    delta *= 1000000LL;
    uint64_t freq = tsc_calc_acpi_timer_ref(delta, ctx->ref_start, ref_stop);
    TRACE("tsc: Refined TSC %lu.%03lu MHz", freq / 1000, freq % 1000);
    g_tsc_freq_hz = freq * 1000;

    // recalibrate the lapic timer
    lapic_timer_recalibrate();

    free_type(tsc_refine_ctx_t, ctx);
}

err_t init_tsc(void) {
    err_t err = NO_ERROR;

    g_tsc_freq_hz = quick_acpi_timer_calibrate();
    CHECK(g_tsc_freq_hz != 0);
    TRACE("timer: Fast TSC calibration using ACPI Timer %lu.%03lu MHz",
        g_tsc_freq_hz / 1000000, (g_tsc_freq_hz / 1000) % 1000);

cleanup:
    return err;
}

err_t tsc_refine(void) {
    err_t err = NO_ERROR;

    // setup
    tsc_refine_ctx_t* ctx = alloc_type(tsc_refine_ctx_t);
    CHECK(ctx != NULL);
    ctx->tsc_start = tsc_read_refs(&ctx->ref_start);
    timer_set(&ctx->timer, tsc_refine_callback, tsc_ms_deadline(1000));

cleanup:
    return err;
}

bool tsc_deadline_is_supported(void) {
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
