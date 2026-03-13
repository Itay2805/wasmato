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

/**
 * Allow getting IPIs from kernel mode
 *
 * We essentially move the TPL to be higher than any usermode interrupt
 * and enable interrupts, so only IPIs can fire
 */
static inline void ipi_enable(void) {
    __writecr8((INTR_VECTOR_IPI >> 4) - 1);
    irq_enable();
}

/**
 * Disable getting IPIs in kernel mode (does
 * not affect usermode)
 *
 * We just disable interrupts and restore the TPL
 */
static inline void ipi_disable(void) {
    irq_disable();
    __writecr8(0);
}
