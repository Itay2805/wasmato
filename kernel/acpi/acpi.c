#include "acpi.h"
#include "acpi_tables.h"

#include "limine_requests.h"
#include "arch/paging.h"
#include "mem/internal/direct.h"
#include "mem/internal/phys_map.h"
#include "mem/internal/virt.h"

/**
 * The frequency of the acpi timer
 */
#define ACPI_TIMER_FREQUENCY  3579545

static size_t m_rsdp_size;

/**
 * The timer port
 */
static uint16_t m_acpi_timer_port;

static err_t validate_acpi_table(acpi_description_header_t* header) {
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

err_t init_acpi_tables() {
    err_t err = NO_ERROR;

    // we need the direct map to look at the tables
    unlock_direct_map();

    CHECK(g_limine_rsdp_request.response != NULL);
    acpi_rsdp_t* rsdp = g_limine_rsdp_request.response->address;

    // calculate the size nicely
    m_rsdp_size = rsdp->revision >= 2 ? rsdp->length : 20;

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
            default: break;
        }
    }

    // validate we got everything
    CHECK(facp != NULL);

    CHECK(facp->pm_tmr_blk != 0);
    CHECK(facp->pm_tmr_len == 4);
    m_acpi_timer_port = facp->pm_tmr_blk;

cleanup:
    lock_direct_map();

    return err;
}

uint32_t acpi_get_timer_tick() {
    return __indword(m_acpi_timer_port);
}

void acpi_stall(uint64_t microseconds) {
    uint32_t delay = (microseconds * ACPI_TIMER_FREQUENCY) / 1000000u;
    uint32_t times = delay >> 22;
    delay &= BIT22 - 1;
    do {
        uint32_t ticks = acpi_get_timer_tick() + delay;
        delay = BIT22;
        while (((ticks - acpi_get_timer_tick()) & BIT23) == 0) {
            cpu_relax();
        }
    } while (times-- > 0);
}
