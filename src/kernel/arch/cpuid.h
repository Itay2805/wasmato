#pragma once

#include <stdint.h>
#include <cpuid.h>

#define CPUID_EXTENDED_CPU_SIG  0x80000001

typedef union {
    ///
    /// Individual bit fields
    ///
    struct {
        uint32_t : 11;
        ///
        /// [Bit 11] SYSCALL/SYSRET available in 64-bit mode.
        ///
        uint32_t SYSCALL_SYSRET : 1;
        uint32_t : 8;
        ///
        /// [Bit 20] Execute Disable Bit available.
        ///
        uint32_t NX : 1;
        uint32_t : 5;
        ///
        /// [Bit 26] 1-GByte pages are available if 1.
        ///
        uint32_t Page1GB : 1;
        ///
        /// [Bit 27] RDTSCP and IA32_TSC_AUX are available if 1.
        ///
        uint32_t RDTSCP : 1;
        uint32_t : 1;
        ///
        /// [Bit 29] Intel(R) 64 Architecture available if 1.
        ///
        uint32_t LM : 1;
        uint32_t : 2;
    };
    uint32_t packed;
} CPUID_EXTENDED_CPU_SIG_EDX;

#define CPUID_VIR_PHY_ADDRESS_SIZE  0x80000008

typedef union {
    struct {
        uint32_t physical_address_bits : 8;
        uint32_t linear_address_bits : 8;
        uint32_t : 16;
    };
    uint32_t packed;
} CPUID_VIR_PHY_ADDRESS_SIZE_EAX;
