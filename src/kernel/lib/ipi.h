#pragma once
#include "arch/intr.h"
#include "arch/intrin.h"

typedef enum ipi_reason {
    /**
     * Perform a TLB flush
     */
    IPI_REASON_TLB_FLUSH,

    /**
     * Ensure all cores have interrupts enabled, and thus
     * are actually after the scheduler init by ensuring
     * all of them can get the ipi
     */
    IPI_SYNC_EARLY_DONE,
} ipi_reason_t;

/**
 * broadcast the ipi to all cores but the
 * current core, returns immediately after
 * issueing it, must call wait to ensure we
 * can run more ipis later
 */
void ipi_broadcast(ipi_reason_t ipi);

/**
 * handle IPI interrupt
 */
void ipi_handle(void);
