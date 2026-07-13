#include "ioapic.h"
#include "acpi/acpi_tables.h"
#include "irq/irq.h"
#include "lib/assert.h"
#include "lib/defs.h"
#include "lib/except.h"
#include "lib/log.h"
#include "lib/pcpu.h"
#include "mem/alloc.h"
#include "mem/direct.h"
#include "mem/early.h"
#include "mem/mappings.h"
#include "mem/phys_map.h"
#include "mem/virt.h"
#include "mem/vmar.h"
#include "sync/spinlock.h"
#include "uapi/mapping.h"
#include "uapi/page.h"
#include "uapi/syscall.h"
#include <stdint.h>

#define IOAPIC_INDEX_OFFSET  0x00
#define IOAPIC_DATA_OFFSET   0x10

#define IO_APIC_IDENTIFICATION_REGISTER_INDEX  0x00
#define IO_APIC_VERSION_REGISTER_INDEX         0x01
#define IO_APIC_REDIRECTION_TABLE_ENTRY_INDEX  0x10

#define IO_APIC_DELIVERY_MODE_FIXED            0
#define IO_APIC_DELIVERY_MODE_LOWEST_PRIORITY  1
#define IO_APIC_DELIVERY_MODE_SMI              2
#define IO_APIC_DELIVERY_MODE_NMI              4
#define IO_APIC_DELIVERY_MODE_INIT             5
#define IO_APIC_DELIVERY_MODE_EXTINT           7

typedef union {
    struct {
        uint32_t : 24;
        uint32_t id : 4;
        uint32_t : 4;
    };
    uint32_t packed;
} PACKED IO_APIC_IDENTIFICATION;

typedef union {
    struct {
        uint32_t version : 8;
        uint32_t : 8;
        uint32_t maximum_redirection_entry : 8;
        uint32_t : 8;
    };
    uint32_t packed;
} PACKED IO_APIC_VERSION;

typedef union {
    struct {
        uint32_t vector : 8;
        uint32_t delivery_mode : 3;
        uint32_t destination_mode : 1;
        uint32_t delivery_status : 1;
        uint32_t polarity : 1;
        uint32_t remote_irr : 1;
        uint32_t trigger_mode : 1;
        uint32_t mask : 1;
        uint32_t : 15;
        uint32_t : 24;
        uint32_t destination_id : 8;
    };
    struct {
        uint32_t packed_low;
        uint32_t packed_high;
    };
    uint64_t packed;
} PACKED IO_APIC_REDIRECTION_TABLE_ENTRY;

typedef struct ioapic {
    void* mapping;
    uint32_t gsi_base;
    uint32_t gsi_end;
    irq_spinlock_t lock;
} ioapic_t;

typedef struct ioapic_override {
    uint32_t gsi;
    ioapic_polarity_t polarity;
    ioapic_trigger_mode_t trigger_mode;
    uint8_t source;
} ioapic_override_t;

/**
 * The list of interrupt overrides
 */
static ioapic_override_t m_ioapic_overrides[8];
static uint8_t m_ioapic_overrides_count = 0;

/**
 * The list of io-apics we have in the system
 */
static ioapic_t m_ioapics[8];
static uint8_t m_ioapics_count = 0;

static uint32_t ioapic_read(void* mapping, uint32_t index) {
    *(volatile uint32_t*)(mapping + IOAPIC_INDEX_OFFSET) = index;
    return *(volatile uint32_t*)(mapping + IOAPIC_DATA_OFFSET);
}

static void ioapic_write(void* mapping, uint32_t index, uint32_t value) {
    *(volatile uint32_t*)(mapping + IOAPIC_INDEX_OFFSET) = index;
    *(volatile uint32_t*)(mapping + IOAPIC_DATA_OFFSET) = value;
}

INIT_CODE err_t ioapic_add(uint64_t ioapic_address, uint32_t gsi_base) {
    err_t err = NO_ERROR;

    // validate is already aligned, makes the rest of the code easier
    uintptr_t addr = ALIGN_DOWN(ioapic_address, PAGE_SIZE);
    CHECK(addr == ioapic_address);

    CHECK(m_ioapics_count < ARRAY_LENGTH(m_ioapics));
    uint8_t index = m_ioapics_count++;
    ioapic_t* new = &m_ioapics[index];
    new->gsi_base = gsi_base;
    // new->gsi_end = gsi_base + ver.maximum_redirection_entry - 1;
    new->mapping = phys_to_direct(addr);
    new->lock = IRQ_SPINLOCK_INIT;

cleanup:
    return err;
}

INIT_CODE err_t ioapic_add_override(
    uint8_t source, uint32_t gsi, 
    ioapic_polarity_t polarity, 
    ioapic_trigger_mode_t trigger_mode
) {
    err_t err = NO_ERROR;

    CHECK(m_ioapic_overrides_count < ARRAY_LENGTH(m_ioapic_overrides));
    uint8_t index = m_ioapic_overrides_count++;
    ioapic_override_t* override = &m_ioapic_overrides[index];
    override->gsi = gsi;
    override->source = source;
    override->polarity = polarity;
    override->trigger_mode = trigger_mode;

cleanup:
    return err;
}

INIT_CODE err_t init_ioapic(void) {
    err_t err = NO_ERROR;

    for (int i = 0; i < m_ioapics_count; i++) {
        ioapic_t* ioapic = &m_ioapics[i];

        // actually prepare the mapping
        void* mapping = ioapic->mapping;
        uintptr_t phys = direct_to_phys(mapping);

        // convert to an ioapic entry
        phys_map_type_t type;
        RETHROW(phys_map_get_type(phys, PAGE_SIZE,  &type));
        CHECK(type == PHYS_MAP_UNUSED);
        phys_map_convert(PHYS_MAP_MMIO_IOAPIC, phys, PAGE_SIZE);
        RETHROW(virt_map_direct(mapping, true));

        // get the maximum gsi
        IO_APIC_VERSION ver = { .packed = ioapic_read(mapping, IO_APIC_VERSION_REGISTER_INDEX) };
        IO_APIC_IDENTIFICATION id = { .packed = ioapic_read(mapping, IO_APIC_IDENTIFICATION_REGISTER_INDEX) };

        // fill in the gsi_end
        ioapic->gsi_end = ioapic->gsi_base + ver.maximum_redirection_entry - 1;

        // make sure they are all masked
        for (int i = 0; i < ver.maximum_redirection_entry; i++) {
            IO_APIC_REDIRECTION_TABLE_ENTRY entry = {
                .mask = 1,
            };
            uint32_t offset = IO_APIC_REDIRECTION_TABLE_ENTRY_INDEX + i * 2;
            ioapic_write(mapping, offset, entry.packed_low);
            ioapic_write(mapping, offset + 1, entry.packed_high);
        }
    }

cleanup:
    return err;
}

void ioapic_register_isa(irq_t* handler, uint8_t isa) {
    // by default we assume identity map the to GSI
    uint32_t gsi = isa;
    ioapic_trigger_mode_t trigger_mode = IOAPIC_EDGE_TRIGGERED;
    ioapic_polarity_t polarity = IOAPIC_ACTIVE_HIGH;

    // search for an override
    for (int i = 0; i < m_ioapic_overrides_count; i++) {
        ioapic_override_t* override = &m_ioapic_overrides[i];
        if (override->source == isa) {
            gsi = override->gsi;
            trigger_mode = override->trigger_mode;
            polarity = override->polarity;
            break;
        }
    }

    // search for the ioapic for this gsi
    ioapic_t* ioapic = NULL;
    int ioapic_index;
    for (ioapic_index = 0; ioapic_index < m_ioapics_count; ioapic_index++) {
        ioapic_t* io = &m_ioapics[ioapic_index];
        if (io->gsi_base <= isa && isa <= io->gsi_end) {
            ioapic = io;
            break;
        }
    }
    ASSERT(ioapic != NULL);

    bool irq_state = irq_spinlock_acquire(&ioapic->lock);

    // setup the redirection entry, it starts as unmasked so we can
    // get interrupts right away
    IO_APIC_REDIRECTION_TABLE_ENTRY entry = {
        .vector = handler->vector,
        .delivery_mode = IO_APIC_DELIVERY_MODE_FIXED,
        .destination_mode = 0,
        .polarity = polarity == IOAPIC_ACTIVE_LOW ? 1 : 0,
        .trigger_mode = trigger_mode == IOAPIC_LEVEL_TRIGGERED ? 1 : 0,
        .mask = 0,
        .destination_id = get_apic_id_of(handler->cpu_id),
    };

    // now write the actual redirection entry
    uint32_t irq = gsi - ioapic->gsi_base;
    uint32_t offset = IO_APIC_REDIRECTION_TABLE_ENTRY_INDEX + irq * 2;
    ioapic_write(ioapic->mapping, offset, entry.packed_low);
    ioapic_write(ioapic->mapping, offset + 1, entry.packed_high);

    // remember the slot and index so we can access it 
    // more easily from the masking code
    handler->type = IRQ_TYPE_IOAPIC;
    handler->ioapic.slot = irq;
    handler->ioapic.index = ioapic_index;

    irq_spinlock_release(&ioapic->lock, irq_state);
}

void ioapic_set_mask(irq_t* handler, bool masked) {
    ioapic_t* ioapic = &m_ioapics[handler->ioapic.index];
    bool irq_state = irq_spinlock_acquire(&ioapic->lock);

    // read-modify-write the mask bit
    IO_APIC_REDIRECTION_TABLE_ENTRY entry = {};
    uint32_t offset = IO_APIC_REDIRECTION_TABLE_ENTRY_INDEX + handler->ioapic.slot * 2;
    entry.packed_low = ioapic_read(ioapic->mapping, offset);
    entry.mask = masked ? 1 : 0;
    ioapic_write(ioapic->mapping, offset, entry.packed_low);

    irq_spinlock_release(&ioapic->lock, irq_state);
}
