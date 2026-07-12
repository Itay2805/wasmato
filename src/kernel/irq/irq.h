#pragma once

#include "lib/defs.h"
#include "uapi/irq.h"
#include "user/object.h"

#include <stdint.h>

typedef enum irq_type : uint8_t {
    /**
     * This IRQ object is not registered yet
     */
    IRQ_TYPE_UNREGISTERED,

    /**
     * IRQ connected to the io-apic
     */
    IRQ_TYPE_IOAPIC,
} irq_type_t;

typedef struct irq {
    /**
     * The object header
     */
    kernel_object_t object;

    union {
        struct {
            /**
             * The slot in the ioapic used
             */
            uint8_t slot;

            /**
             * The index in the io-apic requested
             */
            uint8_t index;
        } ioapic;
    };

    /**
     * The cpu id this interrupt is attached to
     */
    uint32_t cpu_id;

    /**
     * The pointer for the 
     * notification to send
     */
    irq_waiter_t* waiter;

    /**
     * The type of interrupt this is
     */
    irq_type_t type;

    /**
     * The vector of this interrupt
     */
    uint8_t vector;
} irq_t;

/**
 * Setup the actual interrupt handling
 */
INIT_CODE void init_irq_handling(void);

/**
 * Create a new interrupt object, will already allocate a vector, 
 * will not register it yet 
 */
irq_t* irq_create(irq_waiter_t* waiter, int cpu_id);

/**
 * Free the IRQ, unregistering it properly
 */
void irq_free(irq_t* irq);

/**
 * Unmask the given interrupt
 */
void irq_unmask(irq_t* irq);
