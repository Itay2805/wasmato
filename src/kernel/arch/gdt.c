#include "gdt.h"

#include "sync/spinlock.h"
#include "thread/pcpu.h"


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

// TODO: make readonly after boot
static gdt_entries_t m_entries = {
    .kernel_code = {   // kernel code
        .limit          = 0x0000,
        .base_low       = 0x0000,
        .base_mid       = 0x00,
        .access         = 0b10011010,
        .granularity    = 0b00100000,
        .base_high      = 0x00
    },
    .kernel_data = {   // kernel data
        .limit          = 0x0000,
        .base_low       = 0x0000,
        .base_mid       = 0x00,
        .access         = 0b10010010,
        .granularity    = 0b00000000,
        .base_high      = 0x00
    },
    .user_data = {   // user data
        .limit          = 0x0000,
        .base_low       = 0x0000,
        .base_mid       = 0x00,
        .access         = 0b11110010,
        .granularity    = 0b00000000,
        .base_high      = 0x00
    },
    .user_code = {   // user code
        .limit          = 0x0000,
        .base_low       = 0x0000,
        .base_mid       = 0x00,
        .access         = 0b11111010,
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

void init_gdt() {
    gdt_t gdt = {
        .size = sizeof(gdt_entries_t) - 1,
        .entries = &m_entries
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
static spinlock_t m_tss_lock = SPINLOCK_INIT;

/**
 * The tss of the core
 */
__attribute__((aligned(16)))
static CPU_LOCAL tss64_t m_tss = {};

/**
 * per-cpu stacks to use, for special interrupts
 */
__attribute__((aligned(16)))
static CPU_LOCAL char m_stacks[TSS_IST_MAX][SIZE_4KB] = {};

void init_tss(void) {
    tss64_t* tss = pcpu_get_pointer(&m_tss);

    // the ists
    for (tss_ist_t ist = 0; ist < TSS_IST_MAX; ist++) {
        tss->ist[ist] = (uintptr_t)pcpu_get_pointer(&m_stacks[ist]) + SIZE_4KB - 16;
    }

    spinlock_acquire(&m_tss_lock);

    // setup the TSS gdt entry
    m_entries.tss.length = sizeof(tss64_t);
    m_entries.tss.low = (uint16_t)(uintptr_t)tss;
    m_entries.tss.mid = (uint8_t)((uintptr_t)tss >> 16u);
    m_entries.tss.high = (uint8_t)((uintptr_t)tss >> 24u);
    m_entries.tss.upper32 = (uint32_t)((uintptr_t)tss >> 32u);
    m_entries.tss.flags1 = 0b10001001;
    m_entries.tss.flags2 = 0b00000000;

    // load the TSS into the cache
    asm volatile ("ltr %%ax" : : "a"(GDT_TSS) : "memory");

    spinlock_release(&m_tss_lock);
}

void tss_set_irq_stack(void* rsp) {
    m_tss.rsp0 = (uintptr_t)rsp;
}
