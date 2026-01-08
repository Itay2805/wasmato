#pragma once

#include "lib/defs.h"

#include <stdint.h>
#include <lib/except.h>

#define GDT_CODE offsetof(gdt_entries_t, code)
#define GDT_DATA offsetof(gdt_entries_t, data)
#define GDT_TSS offsetof(gdt_entries_t, tss)

typedef struct gdt64_entry {
    uint16_t limit;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} PACKED gdt64_entry_t;

typedef struct tss64_entry {
    uint16_t length;
    uint16_t low;
    uint8_t mid;
    uint8_t flags1;
    uint8_t flags2;
    uint8_t high;
    uint32_t upper32;
    uint32_t reserved;
} PACKED tss64_entry_t;

typedef struct gdt_entries {
    gdt64_entry_t null;
    gdt64_entry_t code;
    gdt64_entry_t data;
    tss64_entry_t tss;
} PACKED gdt_entries_t;

typedef enum tss_ist {
    TSS_IST_IRQ,
    TSS_IST_EXCEPTION,
    TSS_IST_NMI,
    TSS_IST_DB,
    TSS_IST_MAX,
} tss_ist_t;
STATIC_ASSERT(TSS_IST_MAX <= 7);

void init_gdt(void);

void init_tss(void);

void tss_set_irq_stack(void* rsp);
