
#include <cpuid.h>

#include "limine_requests.h"
#include "runtime.h"
#include "acpi/acpi.h"
#include "arch/apic.h"
#include "../common/arch/cpuid.h"
#include "arch/gdt.h"
#include "arch/intr.h"
#include "arch/smp.h"
#include "lib/string.h"
#include "mem/direct.h"
#include "mem/early.h"
#include "mem/phys.h"
#include "mem/phys_map.h"
#include "mem/virt.h"
#include "mem/vmar.h"
#include "lib/pcpu.h"
#include "time/tsc.h"
#include "user/syscall.h"

/**
 * For waiting until all cpus are finished initializing
 */
INIT_DATA static atomic_size_t m_smp_count = 0;

/**
 * If we get any failure then we will mark it
 */
INIT_DATA static atomic_bool m_smp_fail = false;

INIT_CODE static void set_extended_state_features(void) {
    static bool first = true;
    static uint32_t first_xcr0 = 0;
    uint32_t a, b, c, d;

    // enable/disable extended features or something
    CPUID_EXTENDED_STATE_MAIN_LEAF_EAX extended_state_main_leaf_eax = {};
    ASSERT(__get_cpuid_count(
        CPUID_EXTENDED_STATE,
        CPUID_EXTENDED_STATE_MAIN_LEAF,
        &extended_state_main_leaf_eax.raw,
        &b, &c, &d
    ));

    // enable the features we support
    uint64_t xcr0 = 0;
    if (first) TRACE("cpu: extended state:");

    if (extended_state_main_leaf_eax.x87) {
        if (first) TRACE("cpu: - x87");
        xcr0 |= BIT0;
    }

    if (extended_state_main_leaf_eax.SSE) {
        if (first) TRACE("cpu: - SSE");
        xcr0 |= BIT1;
    }

    if (extended_state_main_leaf_eax.AVX) {
        if (first) TRACE("cpu: - AVX");
        xcr0 |= BIT2;
    }

    // ensure that we have a consistent feature view
    if (first) {
        first_xcr0 = xcr0;
    } else {
        ASSERT(first_xcr0 == xcr0);
    }
    __builtin_ia32_xsetbv(0, xcr0);

    first = false;
}

INIT_CODE static void string_verify_features(void) {
    uint32_t eax, ebx, ecx, edx;

    __cpuid_count(7, 0, eax, ebx, ecx, edx);
    if ((ebx & bit_ENH_MOVSB) == 0) WARN("string: Missing enhanced REP MOVSB/STOSB");
    if ((edx & BIT4) == 0) WARN("string: Missing fast short REP MOVSB");

    __cpuid_count(7, 1, eax, ebx, ecx, edx);
    // if ((eax & BIT10) == 0) LOG_WARN("string: Missing zero-length REP MOVSB");
    if ((eax & BIT11) == 0) WARN("string: Missing fast short REP STOSB");
    // if ((eax & BIT12) == 0) LOG_WARN("string: Missing fast short REP CMPSB/CSASB");
}

INIT_CODE static void validate_cpu_features(void) {
    uint32_t a, b, c, d;

    CPUID_VERSION_INFO_ECX version_info_ecx = {};
    ASSERT(__get_cpuid(CPUID_VERSION_INFO, &a, &b, &version_info_ecx.raw, &d));
    ASSERT(version_info_ecx.XSAVE, "Missing XSAVE support");

    CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS_EBX structured_extended_feature_flags_ebx = {};
    CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS_ECX structured_extended_feature_flags_ecx = {};
    ASSERT(__get_cpuid_count(
        CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS,
        CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS_SUB_LEAF_INFO,
        &a,
        &structured_extended_feature_flags_ebx.raw,
        &structured_extended_feature_flags_ecx.raw,
        &d
    ));
    ASSERT(structured_extended_feature_flags_ebx.SMAP, "Missing SMAP support");
    ASSERT(structured_extended_feature_flags_ebx.SMEP, "Missing SMEP support");
    ASSERT(structured_extended_feature_flags_ebx.FSGSBASE, "Missing FSGSBASE support");
    ASSERT(structured_extended_feature_flags_ecx.UMIP, "Missing UMIP support");

    CPUID_EXTENDED_STATE_SUB_LEAF_EAX extended_state_sub_leaf_eax = {};
    ASSERT(__get_cpuid_count(
        CPUID_EXTENDED_STATE,
        CPUID_EXTENDED_STATE_SUB_LEAF,
        &extended_state_sub_leaf_eax.raw,
        &b,
        &c,
        &d
    ));
    ASSERT(extended_state_sub_leaf_eax.XSAVEOPT, "Missing XSAVEOPT support");

    CPUID_EXTENDED_CPU_SIG_EDX extended_cpu_sig_edx = {};
    ASSERT(__get_cpuid(
        CPUID_EXTENDED_CPU_SIG,
        &a, &b, &c,
        &extended_cpu_sig_edx.raw
    ));
    ASSERT(extended_cpu_sig_edx.NX, "Missing NX support");
    ASSERT(extended_cpu_sig_edx.SYSCALL_SYSRET, "Missing SYSCALL/SYSRET support");
}

INIT_CODE static void set_cpu_features(void) {
    // ensure all required cpu features exist
    validate_cpu_features();

    // PG/PE - required for long mode
    // MP - required for SSE
    // WP - write protections
    __writecr0(CR0_PG | CR0_PE | CR0_MP | CR0_WP);

    // PAE - required for long mode
    // OSFXSR/OSXMMEXCPT - required for SSE
    // XSAVE - using xsave
    // SMAP/SMEP - prevent kernel from accessing usermode memory
    // UMIP - prevent usermode from leaking kernel memory
    // FSGSBASE - allow to use {rd,wr}{gs,fs}base
    __writecr4(CR4_PAE | CR4_OSFXSR | CR4_OSXSAVE | CR4_OSXMMEXCPT | CR4_SMAP | CR4_SMEP | CR4_UMIP | CR4_FSGSBASE);

    // setup the efer
    // NXE - Enable NX bit
    // SCE - Enable syscall/sysret opcodes
    MSR_IA32_EFER_REGISTER efer = { .packed = __rdmsr(MSR_IA32_EFER) };
    efer.nxe = 1;
    efer.sce = 1;
    __wrmsr(MSR_IA32_EFER, efer.packed);

    // setup the syscall stuff
    init_syscall();

    // setup extended cpu features
    set_extended_state_features();
}

INIT_CODE static void halt() {
    for (;;) {
        asm("hlt");
    }
}

INIT_CODE static void smp_entry(struct limine_mp_info* info) {
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

    // we are done
    m_smp_count++;

    // we can trigger the scheduler,
    runtime_start();

cleanup:
    // if we got an error mark it
    if (IS_ERROR(err)) {
        m_smp_fail = true;
        m_smp_count++;
    }
    halt();
}

INIT_CODE void _start() {
    err_t err = NO_ERROR;

    // make early logging work
    init_early_pcpu();
    init_early_logging();

    // Welcome!
    TRACE("-------------------------------------------------------------------------------");
    TRACE("TomatOS");
    TRACE("-------------------------------------------------------------------------------");
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
    init_vmar_alloc();

    // we need acpi for some early sleep primitives
    RETHROW(init_acpi_tables());

    // timer subsystem init, we need to start by calibrating the TSC, following
    // by setting up the lapic (including calibration if we don't have TSC deadline)
    // followed by actually setting the timers properly
    RETHROW(init_tsc());
    RETHROW(init_lapic());

    // load the runtime elf, before starting the cores
    RETHROW(load_runtime());

    // perform cpu startup
    CHECK(g_limine_mp_request.response != NULL);
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

    // wait for smp to finish up
    // TODO: timeout?
    while (m_smp_count != g_cpu_count) {
        cpu_relax();
    }
    TRACE("smp: Finished SMP startup");

    // jump to the runtime
    runtime_start();

cleanup:
    halt();
}
