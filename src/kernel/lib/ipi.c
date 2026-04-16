#include "ipi.h"

#include "arch/apic.h"
#include "arch/smp.h"
#include "mem/virt.h"
#include "sync/spinlock.h"
#include "pcpu.h"

/**
 * The lock protects from multiple ipis at the same time
 */
static spinlock_t m_ipi_lock = SPINLOCK_INIT;

/**
 * The reason for the current ipi
 */
static ipi_reason_t m_ipi_reason = 0;

/**
 * How many waiters are currently
 */
static atomic_uint m_ipi_waiter_count = 0;

void ipi_broadcast(ipi_reason_t ipi) {
    // take the lock
    spinlock_acquire(&m_ipi_lock);

    // we are waiting for everyone else
    m_ipi_waiter_count = g_cpu_count - 1;

    // remember the reason
    m_ipi_reason = ipi;

    // TODO: what fence do we want here?

    // trigger the ipi to everyone, we don't do that before core startup
    // because otherwise we will get a spurious ipi later on
    if (g_cpu_count > 1) {
        lapic_send_ipi_all_excluding_self(INTR_VECTOR_IPI);
    }

    // wait for everyone to finish
    // TODO: maybe use a sleep instead
    while (m_ipi_waiter_count != 0) {
        cpu_relax();
    }

    // we can release
    spinlock_release(&m_ipi_lock);
}

void ipi_handle(void) {
    // dispatch the ipi itself
    switch (m_ipi_reason) {
        case IPI_REASON_TLB_FLUSH: virt_handle_tlb_flush_ipi(); break;
        case IPI_SYNC_EARLY_DONE: break;
        default: ASSERT(!"Invalid IPI reason");
    }

    // decrease the waiter
    m_ipi_waiter_count--;
}
