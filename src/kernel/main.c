
#include <cpuid.h>

#include "limine_requests.h"
#include "runtime.h"
#include "acpi/acpi.h"
#include "arch/apic.h"
#include "arch/cpuid.h"
#include "arch/gdt.h"
#include "arch/intr.h"
#include "arch/smp.h"
#include "../runtime/lib/string.h"
#include "mem/direct.h"
#include "mem/early.h"
#include "mem/phys.h"
#include "mem/phys_map.h"
#include "mem/virt.h"
#include "mem/vmar.h"
#include "lib/pcpu.h"
#include "time/tsc.h"
#include "user/stack.h"
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
    INIT_DATA static bool first = true;
    INIT_DATA static uint32_t first_xcr0 = 0;
    uint32_t b, c, d;

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
    INIT_DATA static bool first = true;

    uint32_t a, b, c, d;

    CPUID_VERSION_INFO_ECX version_info_ecx = {};
    ASSERT(__get_cpuid(CPUID_VERSION_INFO, &a, &b, &version_info_ecx.raw, &d));
    ASSERT(version_info_ecx.XSAVE, "Missing XSAVE support");

    // if monitor is supported check that it also has interrupt as break event
    bool has_monitor;
    if (version_info_ecx.MONITOR) {
        CPUID_MONITOR_MWAIT_ECX monitor_mwait_ecx = {};
        if (__get_cpuid(
            CPUID_MONITOR_MWAIT,
            &a,
            &b,
            &monitor_mwait_ecx.raw,
            &d
        )) {
            if (monitor_mwait_ecx.INTERRUPT_AS_BREAK_EVENT) {
                has_monitor = true;
            }
        }
    }

    if (first) {
        g_monitor_supported = has_monitor;
    } else {
        ASSERT(g_monitor_supported == has_monitor);
    }

    CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS_EBX structured_extended_feature_flags_ebx = {};
    CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS_ECX structured_extended_feature_flags_ecx = {};
    CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS_EDX structured_extended_feature_flags_edx = {};
    ASSERT(__get_cpuid_count(
        CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS,
        CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS_SUB_LEAF_INFO,
        &a,
        &structured_extended_feature_flags_ebx.raw,
        &structured_extended_feature_flags_ecx.raw,
        &structured_extended_feature_flags_edx.raw
    ));
    ASSERT(structured_extended_feature_flags_ebx.SMAP, "Missing SMAP support");
    ASSERT(structured_extended_feature_flags_ebx.SMEP, "Missing SMEP support");
    ASSERT(structured_extended_feature_flags_ebx.FSGSBASE, "Missing FSGSBASE support");
    ASSERT(structured_extended_feature_flags_ecx.UMIP, "Missing UMIP support");

    // if we don't have monitor support ensure
    // we have at least waitpkg
    if (!g_monitor_supported) {
        ASSERT(structured_extended_feature_flags_ecx.WAITPKG, "Missing either MONITOR or WAITPKG support");
        // we allow to use C0.2 and we don't give it any max quanta
        // cause we want to give all the control to the runtime
        __wrmsr(MSR_IA32_UMWAIT_CONTROL, 0);
    }

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
    ASSERT(extended_cpu_sig_edx.EXECUTE_DIS, "Missing EXECUTE_DIS support");
    ASSERT(extended_cpu_sig_edx.SYSCALL_SYSRET_64, "Missing SYSCALL/SYSRET support");

    CPUID_EXTENDED_TIME_STAMP_COUNTER_EDX extended_time_stamp_counter_edx = {};
    ASSERT(__get_cpuid(
        CPUID_EXTENDED_TIME_STAMP_COUNTER,
        &a, &b, &c,
        &extended_time_stamp_counter_edx.raw
    ));
    ASSERT(extended_time_stamp_counter_edx.TSC_INVARIANT, "Missing TSC_INVARIANT support");
}

INIT_CODE static void configure_cet(void) {
    INIT_DATA static bool first = true;
    INIT_DATA static bool supported = false;

    uint32_t a, b;
    CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS_ECX structured_extended_feature_flags_ecx = {};
    CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS_EDX structured_extended_feature_flags_edx = {};
    ASSERT(__get_cpuid_count(
        CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS,
        CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS_SUB_LEAF_INFO,
        &a,
        &b,
        &structured_extended_feature_flags_ecx.raw,
        &structured_extended_feature_flags_edx.raw
    ));

    // if CET is not supported at all bail
    if (
        !structured_extended_feature_flags_ecx.CET_SS &&
        !structured_extended_feature_flags_edx.CET_IBT
    ) {
        if (!first) {
            ASSERT(!supported);
        }
        first = true;

        return;
    }

    if (!first) {
        ASSERT(supported);
    }
    supported = true;

    MSR_IA32_CET_REGISTER u_cet = {};
    MSR_IA32_CET_REGISTER s_cet = {};

    // enable IBT on both usermode and kernel mode
    if (structured_extended_feature_flags_edx.CET_IBT) {
        if (first) {
            TRACE("cpu: enabling IBT");
        }

        // enable endbr tracking
        // specifically don't enable notrack prefix, we disable jump tables
        // so it won't be used
        u_cet.ENDBR_EN = 1;
        s_cet.ENDBR_EN = 1;
    }

    // enable shadow stack only for usermode for now
    if (structured_extended_feature_flags_ecx.CET_SS) {
        if (first) {
            TRACE("cpu: enabling Shadow Stacks");
        }

        // mark that shadow stacks are supported
        g_shadow_stack_supported = true;

        // enable usermode shadow stack
        // TODO: how to enable kernel mode shadow stacks correctly?
        u_cet.SH_STK_EN = 1;
    }

    first = false;

    // configure both
    __wrmsr(MSR_IA32_U_CET, u_cet.raw);
    __wrmsr(MSR_IA32_S_CET, s_cet.raw);

    // fully enable CET
    uint32_t cr4 = __readcr4();
    cr4 |= CR4_CET;
    __writecr4(cr4);
}

INIT_CODE static void set_cpu_id(void) {
    uint32_t a, b, d;

    CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS_ECX structured_extended_feature_flags_ecx = {};
    __get_cpuid_count(
        CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS,
        CPUID_STRUCTURED_EXTENDED_FEATURE_FLAGS_SUB_LEAF_INFO,
        &a, &b,
        &structured_extended_feature_flags_ecx.raw,
        &d
    );
    ASSERT(structured_extended_feature_flags_ecx.RDPID, "Missing RDPID support");

    // set the cpuid in the TSC aux for usermode to use
    __wrmsr(MSR_IA32_TSC_AUX, get_cpu_id());
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
    uint32_t cr4 = CR4_PAE | CR4_OSFXSR | CR4_OSXSAVE | CR4_OSXMMEXCPT | CR4_SMAP | CR4_SMEP | CR4_UMIP | CR4_FSGSBASE;
    __writecr4(cr4);

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

    // setup CET
    configure_cet();
}

INIT_CODE static void halt() {
    for (;;) {
        asm("hlt");
    }
}

// called before CET is initialized
OMIT_ENDBR INIT_CODE static void smp_entry(struct limine_mp_info* info) {
    err_t err = NO_ERROR;

    //
    // Start by setting up the per-cpu context
    //
    init_gdt();
    set_cpu_features();
    switch_page_table();
    pcpu_init_per_core(info->extra_argument);
    set_cpu_id();
    init_tss();
    init_idt();

    TRACE("smp: \tCPU#%lu - LAPIC#%d", info->extra_argument, info->lapic_id);

    // and now we can init
    init_lapic_per_core();

    // we are done
    m_smp_count++;

    // wait for all cores to start
    while (m_smp_count != g_cpu_count) {
        cpu_relax();
    }

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

// called before CET is initialized
OMIT_ENDBR INIT_CODE void _start() {
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
    set_cpu_id();

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

    // ensure we only have a single module
    CHECK(g_limine_module_request.response != nullptr);
    CHECK(g_limine_module_request.response->module_count == 1);

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
        } else {
            // start it up
            response->cpus[i]->extra_argument = i;
            response->cpus[i]->goto_address = (void*)smp_entry;
        }
    }

    // wait for smp to finish up
    while (m_smp_count != g_cpu_count - 1) {
        cpu_relax();
    }
    TRACE("smp: Finished SMP startup");
    TRACE("smp: Starting usermode");

    // perform the last increment to let the rest of
    // the cores enter usermode
    m_smp_count++;

    // jump to the runtime
    runtime_start();

cleanup:
    halt();
}
