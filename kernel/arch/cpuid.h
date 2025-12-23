#pragma once

#define CPUID_VIR_PHY_ADDRESS_SIZE  0x80000008
#include <stdint.h>

typedef union {
    struct {
        uint32_t physical_address_bits : 8;
        uint32_t linear_address_bits : 8;
        uint32_t : 16;
    };
    uint32_t packed;
} CPUID_VIR_PHY_ADDRESS_SIZE_EAX;
