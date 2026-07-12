#pragma once

#include "irq/irq.h"
#include "lib/defs.h"
#include "lib/except.h"
#include <stdint.h>

typedef enum ioapic_polarity : uint8_t {
    IOAPIC_ACTIVE_HIGH,
    IOAPIC_ACTIVE_LOW,
} ioapic_polarity_t;

typedef enum ioapic_trigger_mode : uint8_t {
    IOAPIC_EDGE_TRIGGERED,
    IOAPIC_LEVEL_TRIGGERED,
} ioapic_trigger_mode_t;

/**
 * Add another ioapic to the system
 */
INIT_CODE err_t ioapic_add(uint64_t ioapic_address, uint32_t gsi_base);

/**
 * Add an interrupt source override to the system
 */
INIT_CODE err_t ioapic_add_override(
    uint8_t source, uint32_t gsi, 
    ioapic_polarity_t polarity, 
    ioapic_trigger_mode_t trigger_mode
);

/** 
 * Initialize all of the IOAPICs
 */
INIT_CODE err_t init_ioapic(void);

/**
 * Register an ISA interrupt on the given interrupt object
 */
void ioapic_register_isa(irq_t* irq, uint8_t isa);

/**
 * Change the mask mode of an interrupt
 */
void ioapic_set_mask(irq_t* irq, bool masked);
