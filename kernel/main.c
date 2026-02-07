
#include <cpuid.h>

#include "limine_requests.h"
#include "acpi/acpi.h"
#include "arch/apic.h"
#include "arch/gdt.h"
#include "arch/intr.h"
#include "arch/smp.h"
#include "lib/string.h"
#include "mem/internal/direct.h"
#include "mem/internal/early.h"
#include "mem/internal/phys.h"
#include "mem/internal/phys_map.h"
#include "mem/internal/virt.h"
#include "thread/pcpu.h"
#include "thread/scheduler.h"
#include "thread/thread.h"
#include "time/timer.h"
#include "time/tsc.h"
/**
 * The init thread
 */
static thread_t* m_init_thread;

static void init_thread_entry(void* arg) {
    err_t err = NO_ERROR;

    TRACE("Init thread started");

    // no longer need any of the bootloader memory
    // at this point
    RETHROW(reclaim_bootloader_memory());

    // TODO: reclaim init code

    // for fun and profit
    phys_map_dump();
    region_dump(&g_kernel_memory);

cleanup:
    ASSERT(!IS_ERROR(err));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Early startup
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * For waiting until all cpus are finished initializing
 */
static atomic_size_t m_smp_count = 0;

/**
 * If we get any failure then we will mark it
 */
static atomic_bool m_smp_fail = false;

typedef struct xcr0_feature {
    const char* name;
    bool enable;
    bool required;
} xcr0_feature_t;

/**
 * The features that we support and want to enable if supported
 */
static const xcr0_feature_t m_xcr0_features[] = {
    [0] = { "x87", true, true },
    [1] = { "SSE", true, true },
    [2] = { "AVX", true, true },
    [3] = { "MPX[BNDREG]", false, false },
    [4] = { "MPX[BNDCSR]", false, false },
    [5] = { "AVX-512[OPMASK]", false, false },
    [6] = { "AVX-512[ZMM_Hi256]", false, false },
    [7] = { "AVX-512[Hi16_ZMM]", false, false },
    [8] = { "PT", false, false },
    [9] = { "PKRU", false, false },
    [10] = { "PASID", false, false },
    [11] = { "CET[U]", false, false },
    [12] = { "CET[S]", false, false },
    [13] = { "HDC", false, false },
    [14] = { "UINTR", false, false },
    [15] = { "LBR", false, false },
    [16] = { "HWP", false, false },
    [17] = { "AMX[TILECFG]", false, false },
    [18] = { "AMX[XTILEDATA]", false, false },
    [19] = { "APX", false, false },
};

static void set_extended_state_features(void) {
    static bool first = true;
    static uint32_t first_xcr0 = 0;
    uint32_t a, b, c, d;

    // ensure we have xsave (for the basic support sutff)
    __cpuid(1, a, b, c, d);
    ASSERT(c & bit_XSAVE, "Missing support for xsave");

    // we are going to force xsaveopt for now
    __cpuid_count(0xD, 1, a, b, c, d);
    ASSERT(a & bit_XSAVEOPT, "Missing support for xsaveopt");

    // enable/disable extended features or something
    if (first) TRACE("cpu: extended state:");
    __cpuid_count(0xD, 0, a, b, c, d);
    uint64_t xcr0 = 0;
    uint64_t features = a | ((uint64_t)d << 32);
    for (int i = 0; i < ARRAY_LENGTH(m_xcr0_features); i++) {
        const xcr0_feature_t* feature = &m_xcr0_features[i];
        uint64_t bit = 1 << i;
        if ((features & bit) != 0) {
            if (feature->enable) {
                xcr0 |= bit;
                if (first) TRACE("cpu: \t- %s [enabling]", feature->name);
            } else {
                if (first) TRACE("cpu: \t- %s", feature->name);
            }
        } else {
            ASSERT(!feature->required, "Missing required feature %s", feature->name);
        }
    }

    // ensure that we have a consistent feature view
    if (first) {
        first_xcr0 = xcr0;
    } else {
        ASSERT(first_xcr0 == xcr0);
    }
    __builtin_ia32_xsetbv(0, xcr0);

    if (first) {
        __cpuid_count(0xD, 0, a, b, c, d);
        TRACE("cpu: extended state size is %d bytes", b);
        g_extended_state_size = b;
        ASSERT((g_extended_state_size + sizeof(thread_t)) <= PAGE_SIZE);
    }

    first = false;
}

static void set_cpu_features(void) {
    // PG/PE - required for long mode
    // MP - required for SSE
    // WP - write protections
    __writecr0(CR0_PG | CR0_PE | CR0_MP | CR0_WP);

    // ensure we have support for smap
    // ensure we have xsave (for the basic support sutff)
    uint32_t a, b, c, d;
    __cpuid(1, a, b, c, d);
    ASSERT(c & bit_XSAVE, "Missing support for xsave");

    // PAE - required for long mode
    // OSFXSR/OSXMMEXCPT - required for SSE
    // XSAVE - using xsave
    // SMAP/SMEP - prevent kernel from accessing usermode memory
    // UMIP - prevent usermode from leaking kernel memory
    __writecr4(CR4_PAE | CR4_OSFXSR | CR4_OSXSAVE | CR4_OSXMMEXCPT | CR4_SMAP | CR4_SMEP | CR4_UMIP);

    set_extended_state_features();
}

static void halt() {
    for (;;) {
        asm("hlt");
    }
}

static void smp_entry(struct limine_mp_info* info) {
    err_t err = NO_ERROR;

    //
    // Start by setting up the per-cpu context
    //
    init_gdt();
    set_cpu_features();
    switch_page_table();
    pcpu_init_per_core(info->extra_argument);
    init_tss();
    init_idt();

    TRACE("smp: \tCPU#%lu - LAPIC#%d", info->extra_argument, info->lapic_id);

    // and now we can init
    init_lapic_per_core();
    RETHROW(scheduler_init_per_core());

    // we are done
    m_smp_count++;

    // we can trigger the scheduler,
    scheduler_start_per_core();

cleanup:
    // if we got an error mark it
    if (IS_ERROR(err)) {
        m_smp_fail = true;
        m_smp_count++;
    }
    halt();
}

void _start() {
    err_t err = NO_ERROR;

    // make early logging work
    init_early_pcpu();
    init_early_logging();

    // Welcome!
    TRACE("------------------------------------------------------------------------------------------------------------");
    TRACE("TomatOS");
    TRACE("------------------------------------------------------------------------------------------------------------");
    limine_check_revision();

    //
    // early cpu init, this will take care of
    // having interrupts and a valid GDT already
    //
    init_gdt();
    init_tss();
    init_idt();

    //
    // Setup the cpu features
    //
    string_verify_features();
    set_cpu_features();

    //
    // setup the basic memory management
    //
    RETHROW(init_early_mem());
    RETHROW(init_phys());
    RETHROW(init_virt());
    RETHROW(init_phys_map());
    init_region_alloc();

    // we need acpi for some early sleep primitives
    RETHROW(init_acpi_tables());

    // timer subsystem init, we need to start by calibrating the TSC, following
    // by setting up the lapic (including calibration if we don't have TSC deadline)
    // followed by actually setting the timers properly
    RETHROW(init_tsc());
    RETHROW(init_lapic());
    init_timers();
    RETHROW(tsc_refine());

    // setup the scheduler structures
    RETHROW(init_scheduler());

    // perform cpu startup
    if (g_limine_mp_request.response != NULL) {
        struct limine_mp_response* response = g_limine_mp_request.response;

        g_cpu_count = response->cpu_count;
        TRACE("smp: Starting CPUs (%zu)", g_cpu_count);

        // setup pcpu for the rest of the system
        RETHROW(init_pcpu(g_limine_mp_request.response->cpu_count));

        for (size_t i = 0; i < g_cpu_count; i++) {
            if (response->cpus[i]->lapic_id == response->bsp_lapic_id) {
                TRACE("smp: \tCPU#%zu - LAPIC#%d (BSP)", i, response->cpus[i]->lapic_id);

                // allocate the per-cpu storage now that we know our id
                init_lapic_per_core();
                RETHROW(scheduler_init_per_core());

                m_smp_count++;
            } else {
                // start it up
                response->cpus[i]->extra_argument = i;
                response->cpus[i]->goto_address = smp_entry;
            }

            while (m_smp_count != i + 1) {
                cpu_relax();
            }
        }
    } else {
        // no SMP startup available from bootloader,
        // just assume we have a single cpu
        WARN("smp: missing limine SMP support");
        RETHROW(init_pcpu(1));
    }

    // wait for smp to finish up
    // TODO: timeout?
    while (m_smp_count != g_cpu_count) {
        cpu_relax();
    }
    TRACE("smp: Finished SMP startup");

    // we are about done, create the init thread and queue it
    RETHROW(thread_create(&m_init_thread, init_thread_entry, NULL, "init thread"));
    scheduler_wakeup_thread(m_init_thread);

    // and we are ready to start the scheduler
    scheduler_start_per_core();

cleanup:
    halt();
}
