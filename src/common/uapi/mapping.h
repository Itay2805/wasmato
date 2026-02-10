#pragma once

#include <stdint.h>

/**
 * The protections of the mapping
 */
typedef enum mapping_protection : uint8_t {
    /**
     * This is a read-write region
     */
    MAPPING_PROTECTION_RW,

    /**
     * This is a read-only region
     */
    MAPPING_PROTECTION_RO,

    /**
     * This is a read-execute region
     */
    MAPPING_PROTECTION_RX,
} mapping_protection_t;

/**
 * The caching policy for the mapping
 */
typedef enum mapping_cache_policy : uint8_t {
    // TODO: what should I do with this shit
    MAPPING_CACHE_POLICY_CACHED,
} mapping_cache_policy_t;

