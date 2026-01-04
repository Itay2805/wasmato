#pragma once

#include <stdint.h>
#include <stddef.h>

#include "lib/except.h"

typedef enum phys_map_type {
    PHYS_MAP_UNUSED,

    PHYS_MAP_BAD_RAM,
    PHYS_MAP_RAM,

    PHYS_MAP_MMIO,
    PHYS_MAP_MMIO_LAPIC,
    PHYS_MAP_MMIO_FRAMEBUFFER,

    PHYS_MAP_FIRMWARE_RESERVED,

    PHYS_MAP_ACPI_RECLAIMABLE,
    PHYS_MAP_ACPI_RESERVED,
    PHYS_MAP_ACPI_NVS,

    PHYS_MAP_BOOTLOADER_RECLAIMABLE,
    PHYS_MAP_KERNEL_RESERVED,
} phys_map_type_t;

/**
 * The physical memory map is used to track
 * what areas of memory are used by what
 */
err_t init_phys_map(void);

/**
 * Convert the given range into another type
 */
void phys_map_convert(phys_map_type_t type, uint64_t start, size_t length);

/**
 * Get the type of a given range, if it overlaps the function will error
 */
err_t phys_map_get_type(uint64_t start, size_t length, phys_map_type_t* type);

typedef err_t (*phys_map_cb_t)(void* ctx, phys_map_type_t type, uint64_t start, size_t length);

/**
 * Iterate the physical memory map
 */
err_t phys_map_iterate(phys_map_cb_t cb, void* ctx);

void phys_map_dump(void);

/**
 * The amount of physical hardware space we have
 */
uint8_t get_physical_address_bits(void);
