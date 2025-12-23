#pragma once

#include "lib/except.h"
#include <stdbool.h>

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

extern const char* g_phys_map_type_str[];

/**
 * Initialize the physical memory allocator
 */
err_t phys_init(void);

/**
 * Free memory reserved by the bootloader
 */
void phys_free_bootloader_reserved(void);

/**
 * Convert the given range into another type
 */
void phys_map_convert(phys_map_type_t type, uint64_t start, size_t length);

/**
 * Get the type of a given range, if it overlaps the function will error
 */
err_t phys_map_get_type(uint64_t start, size_t length, phys_map_type_t* type);

/**
 * Get the amount of physical address bit the cpu has
 */
uint8_t get_physical_address_bits(void);

/**
 * Allocate physical memory
 */
void* phys_alloc(size_t size);

/**
 * Free physical memory
 */
void phys_free(void* ptr, size_t size);

typedef err_t (*phys_map_cb_t)(void* ctx, phys_map_type_t type, uint64_t start, size_t length);

err_t phys_map_iterate(phys_map_cb_t cb, void* ctx);

void dump_phys_map(void);
