#include "acpi.h"
#include "acpi_tables.h"

#include "irq/ioapic.h"
#include "lib/except.h"
#include "limine_requests.h"
#include "arch/paging.h"
#include "mem/direct.h"
#include "mem/phys_map.h"

/**
 * The frequency of the acpi timer
 */
#define ACPI_TIMER_FREQUENCY  3579545

/**
 * The timer port
 */
INIT_DATA static uint16_t m_acpi_timer_port;

INIT_CODE static err_t validate_acpi_table(acpi_description_header_t* header) {
    err_t err = NO_ERROR;

    // validate the header length
    CHECK(header->length >= sizeof(acpi_description_header_t));

    // validate the checksums
    uint8_t checksum = 0;
    for (size_t i = 0; i < header->length; i++) {
        checksum += *((uint8_t*)header + i);
    }
    CHECK(checksum == 0);

cleanup:
    return err;
}

typedef struct address64 {
    uint64_t value;
} PACKED address64_t;

typedef struct address32 {
    uint32_t value;
} PACKED address32_t;

static INIT_CODE err_t acpi_parse_madt(acpi_madt_header_t* madt) {
    err_t err = NO_ERROR;

    uint8_t* data = (uint8_t*)(madt + 1);                
    int64_t bytes_left = madt->header.length - sizeof(acpi_madt_header_t);
    while (bytes_left >= 2) {
        uint8_t type = data[0];
        uint8_t length = data[1];

        CHECK(length <= bytes_left);
        CHECK(length >= 2);

        if (type == ACPI_IO_APIC) {
            CHECK(length == sizeof(acpi_io_apic_t));
            acpi_io_apic_t* io_apic = (acpi_io_apic_t*)data;
            RETHROW(ioapic_add(io_apic->io_apic_address, io_apic->gsi_base));

        } else if (type == ACPI_INTERRUPT_SOURCE_OVERRIDE) {
            CHECK(length == sizeof(acpi_interrupt_source_override_t));
            acpi_interrupt_source_override_t* iso = (acpi_interrupt_source_override_t*)data;

            CHECK(iso->bus == 0); // ISA bus
            ioapic_polarity_t polarity = IOAPIC_ACTIVE_HIGH;
            ioapic_trigger_mode_t trigger_mode = IOAPIC_EDGE_TRIGGERED;

            switch ((iso->flags >> ACPI_POLARITY) & 0b11) {
                case 0b00: break;
                case 0b01: polarity = IOAPIC_ACTIVE_HIGH; break;
                case 0b10: CHECK_FAIL("Invalid ACPI ISO polarity"); break;
                case 0b11: polarity = IOAPIC_ACTIVE_LOW; break;
                default: __builtin_unreachable();
            }
            
            switch ((iso->flags >> ACPI_TRIGGER_MODE) & 0b11) {
                case 0b00: break;
                case 0b01: trigger_mode = IOAPIC_EDGE_TRIGGERED; break;
                case 0b10: CHECK_FAIL("Invalid ACPI ISO polarity"); break;
                case 0b11: trigger_mode = IOAPIC_LEVEL_TRIGGERED; break;
                default: __builtin_unreachable();
            }

            RETHROW(ioapic_add_override(iso->source, iso->gsi, polarity, trigger_mode));
        }

        data += length;
        bytes_left -= length;
    }


cleanup:
    return err;
}

INIT_CODE err_t init_acpi_tables() {
    err_t err = NO_ERROR;

    CHECK(g_limine_rsdp_request.response != NULL);
    acpi_rsdp_t* rsdp = g_limine_rsdp_request.response->address;

    // save up the rsdp for future use
    CHECK(rsdp->signature == ACPI_RSDP_SIGNATURE);
    CHECK(rsdp->rsdt_address != 0);

    // the tables we need for early init
    acpi_facp_t* facp = NULL;

    // get either the xsdt or rsdt based on the revision
    acpi_description_header_t* xsdt = NULL;
    acpi_description_header_t* rsdt = NULL;
    if (rsdp->revision >= 2) {
        xsdt = phys_to_direct(rsdp->xsdt_address);
        RETHROW(validate_acpi_table(xsdt));
    } else {
        rsdt = phys_to_direct(rsdp->rsdt_address);
        RETHROW(validate_acpi_table(rsdt));
    }

    // calculate the entry count
    size_t entry_count;
    if (xsdt != NULL) {
        entry_count = (xsdt->length - sizeof(acpi_description_header_t)) / sizeof(void*);
    } else {
        entry_count = (rsdt->length - sizeof(acpi_description_header_t)) / sizeof(uint32_t);
    }

    // pass over the table, validating the tables and finding the
    // tables we do need right now
    for (size_t i = 0; i < entry_count; i++) {
        acpi_description_header_t* table;
        if (xsdt != NULL) {
            address64_t* addrs = (address64_t*)(xsdt + 1);
            table = phys_to_direct(addrs[i].value);
        } else if (rsdt != NULL) {
            address32_t* addrs = (address32_t*)(rsdt + 1);
            table = phys_to_direct(addrs[i].value);
        } else {
            CHECK_FAIL();
        }

        // do we need this
        switch (table->signature) {
            case ACPI_FACP_SIGNATURE: {
                RETHROW(validate_acpi_table(table));
                facp = (acpi_facp_t*)table;
            } break;

            case ACPI_MADT_SIGNATURE: {
                RETHROW(validate_acpi_table(table));
                RETHROW(acpi_parse_madt((acpi_madt_header_t*)table));
            } break;

            default: 
                break;
        }
    }

    // validate we got everything
    CHECK(facp != NULL);

    CHECK(facp->pm_tmr_blk != 0);
    CHECK(facp->pm_tmr_len == 4);
    m_acpi_timer_port = facp->pm_tmr_blk;

cleanup:
    return err;
}

INIT_CODE uint32_t acpi_get_timer_tick() {
    return __indword(m_acpi_timer_port);
}
