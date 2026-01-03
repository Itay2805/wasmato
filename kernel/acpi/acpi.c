#include "acpi.h"

#include <limine_requests.h>

#include "limine.h"
#include "lib/defs.h"

#include "acpi_tables.h"
#include "mem/memory.h"
#include "arch/intrin.h"
#include "lib/string.h"
#include "mem/phys.h"
#include "mem/virt.h"

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

static err_t acpi_map_tables(void* ctx, phys_map_type_t type, uintptr_t start, size_t length) {
    err_t err = NO_ERROR;

    if (type == PHYS_MAP_ACPI_RESERVED || type == PHYS_MAP_ACPI_RECLAIMABLE) {
        start = ALIGN_DOWN(start, PAGE_SIZE);
        void* map_start = PHYS_TO_DIRECT(start);
        void* map_end = PHYS_TO_DIRECT(ALIGN_UP(start + length, PAGE_SIZE));
        size_t num_pages = (map_end - map_start) / PAGE_SIZE;
        RETHROW(virt_map(map_start, start, num_pages, MAP_FLAG_WRITEABLE, VIRT_MAP_STRICT));
    }

cleanup:
    return err;
}

static err_t acpi_unmap_tables(void* ctx, phys_map_type_t type, uintptr_t start, size_t length) {
    err_t err = NO_ERROR;

    if (type == PHYS_MAP_ACPI_RESERVED || type == PHYS_MAP_ACPI_RECLAIMABLE) {
        void* map_start = PHYS_TO_DIRECT(ALIGN_DOWN(start, PAGE_SIZE));
        void* map_end = PHYS_TO_DIRECT(ALIGN_UP(start + length, PAGE_SIZE));
        size_t num_pages = (map_end - map_start) / PAGE_SIZE;
        RETHROW(virt_unmap(map_start, num_pages, VIRT_UNMAP_STRICT));
    }

cleanup:
    return err;
}

err_t init_acpi_tables() {
    err_t err = NO_ERROR;

    RETHROW(phys_map_iterate(acpi_map_tables, NULL));

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
        xsdt = PHYS_TO_DIRECT(rsdp->xsdt_address);
        RETHROW(validate_acpi_table(xsdt));
    } else {
        rsdt = PHYS_TO_DIRECT(rsdp->rsdt_address);
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
            table = PHYS_TO_DIRECT(((acpi_description_header_t**)(xsdt + 1))[i]);
        } else if (rsdt != NULL) {
            table = PHYS_TO_DIRECT(((uint32_t*)(rsdt + 1))[i]);
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
    ASSERT(!IS_ERROR(phys_map_iterate(acpi_unmap_tables, NULL)));

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
