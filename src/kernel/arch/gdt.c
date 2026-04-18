#include "gdt.h"

#include "lib/defs.h"
#include "lib/except.h"
#include "mem/stack.h"
#include "sync/spinlock.h"
#include "lib/pcpu.h"
#include "lib/printf.h"
#include <stdint.h>

#include "lib/string.h"
#include "mem/alloc.h"
#include "mem/mappings.h"
#include "mem/vmar.h"

typedef struct tss64 {
    uint32_t reserved_1;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved_2;
    uint64_t ist[7];
    uint64_t reserved_3;
    uint32_t iopb_offset;
} PACKED tss64_t;
STATIC_ASSERT(sizeof(tss64_t) == 104);

typedef struct gdt {
    uint16_t size;
    gdt_entries_t* entries;
} PACKED gdt_t;

LATE_RO static gdt_entries_t m_gdt_entries = {
    .kernel_code = {   // kernel code
        .limit          = 0x0000,
        .base_low       = 0x0000,
        .base_mid       = 0x00,
        .access         = 0b10011011,
        .granularity    = 0b00100000,
        .base_high      = 0x00
    },
    .kernel_data = {   // kernel data
        .limit          = 0x0000,
        .base_low       = 0x0000,
        .base_mid       = 0x00,
        .access         = 0b10010011,
        .granularity    = 0b00000000,
        .base_high      = 0x00
    },
    .user_data = {   // user data
        .limit          = 0x0000,
        .base_low       = 0x0000,
        .base_mid       = 0x00,
        .access         = 0b11110011,
        .granularity    = 0b00000000,
        .base_high      = 0x00
    },
    .user_code = {   // user code
        .limit          = 0x0000,
        .base_low       = 0x0000,
        .base_mid       = 0x00,
        .access         = 0b11111011,
        .granularity    = 0b00100000,
        .base_high      = 0x00
    },
    .tss = {   // TSS
        .length         = 0,
        // Will be filled by the init function
        .low            = 0,
        .mid            = 0,
        .high           = 0,
        .upper32        = 0,
        .flags1         = 0b10001001,
        .flags2         = 0b00000000,
        .reserved       = 0
    }
};

INIT_CODE void init_gdt() {
    gdt_t gdt = {
        .size = sizeof(gdt_entries_t) - 1,
        .entries = &m_gdt_entries
    };
    asm volatile (
        "lgdt %0\n"
        "movq %%rsp, %%rax\n"
        "pushq %2\n"
        "pushq %%rax\n"
        "pushfq\n"
        "pushq %1\n"
        "lea 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "iretq\n"
        "1:\n"
        "movw %2, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        :
        : "m"(gdt)
        , "i"(GDT_KERNEL_CODE)
        , "i"(GDT_KERNEL_DATA)
        : "memory", "rax"
    );
}

/**
 * We are using the same gdt entry for each core, so we can't
 * have two cores loading it at the same time
 */
INIT_DATA static spinlock_t m_tss_lock = SPINLOCK_INIT;

/**
 * The tss of the core
 * NOTE: this is not static because its accessed from the syscall entry stub
 *       for setting the kernel stack
 */
__attribute__((aligned(16)))
CPU_LOCAL tss64_t m_tss = {};

LATE_RO mem_alloc_t m_ssp_table_alloc = {};

INIT_CODE void init_tss(void) {
    tss64_t* tss = pcpu_get_pointer(&m_tss);

    spinlock_acquire(&m_tss_lock);

    // setup the TSS gdt entry
    m_gdt_entries.tss.length = sizeof(tss64_t);
    m_gdt_entries.tss.low = (uint16_t)(uintptr_t)tss;
    m_gdt_entries.tss.mid = (uint8_t)((uintptr_t)tss >> 16u);
    m_gdt_entries.tss.high = (uint8_t)((uintptr_t)tss >> 24u);
    m_gdt_entries.tss.upper32 = (uint32_t)((uintptr_t)tss >> 32u);
    m_gdt_entries.tss.flags1 = 0b10001001;
    m_gdt_entries.tss.flags2 = 0b00000000;

    // load the TSS into the cache
    asm volatile ("ltr %%ax" : : "a"(GDT_TSS) : "memory");

    spinlock_release(&m_tss_lock);
}

INIT_CODE err_t init_tss_stacks(void) {
    err_t err = NO_ERROR;

    // if we have shadow pages supported, then allocate an SSP Table
    // for the ISTs
    uint64_t* ssp_table = nullptr;
    if (g_shadow_stack_supported) {
        // the first core also ensures that we have the ssp table
        // alloc actually setup and ready to allocate entries
        if (get_cpu_id() == 0) {
            mem_alloc_init(&m_ssp_table_alloc, sizeof(uintptr_t) * 8, 8);
        }

        // allocate and set the table
        ssp_table = mem_calloc(&m_ssp_table_alloc);
        __wrmsr(MSR_IA32_INTERRUPT_SSP_TABLE_ADDR, (uintptr_t)ssp_table);
    }

    // allocate proper kernel stacks for this, they can be small
    for (tss_ist_t ist = 0; ist < TSS_IST_MAX; ist++) {
        // choose a proper name for the stack
        char name[64] = {};
        switch (ist) {
            case TSS_IST_DF: snprintf(name, sizeof(name), "df-%d", get_cpu_id()); break;
            case TSS_IST_NMI: snprintf(name, sizeof(name), "nmi-%d", get_cpu_id()); break;
            case TSS_IST_DB: snprintf(name, sizeof(name), "db-%d", get_cpu_id()); break;
            case TSS_IST_MCE: snprintf(name, sizeof(name), "mce-%d", get_cpu_id()); break;
            case TSS_IST_CP: snprintf(name, sizeof(name), "cp-%d", get_cpu_id()); break;
            default: CHECK_FAIL();
        }

        // allocate and set the stack, we are going to pre-fault it to enasure
        // it is available when we get something that needs to use it
        stack_alloc_t alloc = {};
        RETHROW(stack_alloc(&alloc, name, SIZE_4KB, STACK_ALLOC_IST));

        m_tss.ist[ist] = (uintptr_t)alloc.stack;

        if (ssp_table != nullptr) {
            ssp_table[ist + 1] = (uintptr_t)alloc.shadow_stack;
        }
    }

cleanup:
    return err;
}

INIT_CODE void protect_ssp_tables() {
    mem_lock(&m_ssp_table_alloc);
}

void tss_set_rsp0(void* rsp) {
    m_tss.rsp0 = (uintptr_t)rsp;
}
