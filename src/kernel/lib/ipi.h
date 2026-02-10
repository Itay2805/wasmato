#pragma once

typedef enum ipi_reason {
    /**
     * Perform a TLB flush
     */
    IPI_REASON_TLB_FLUSH,
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
