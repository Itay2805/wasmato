#include "intr.h"

#include <stdnoreturn.h>

#include "apic.h"
#include "gdt.h"
#include "lib/ipi.h"
#include "lib/log.h"
#include "mem/virt.h"
#include "sync/spinlock.h"
#include "lib/pcpu.h"
#include "mem/stack.h"
#include "time/timer.h"


#define IDT_TYPE_TASK           0x5
#define IDT_TYPE_INTERRUPT_16   0x6
#define IDT_TYPE_TRAP_16        0x7
#define IDT_TYPE_INTERRUPT_32   0xE
#define IDT_TYPE_TRAP_32        0xF

typedef struct idt_entry {
    uint64_t handler_low : 16;
    uint64_t selector : 16;
    uint64_t ist : 3;
    uint64_t _zero1 : 5;
    uint64_t gate_type : 4;
    uint64_t _zero2 : 1;
    uint64_t ring : 2;
    uint64_t present : 1;
    uint64_t handler_high : 48;
    uint64_t _zero3 : 32;
} PACKED idt_entry_t;

typedef struct intr {
    uint16_t limit;
    idt_entry_t* base;
} PACKED idt_t;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Exception handling - has a bunch of code to save registers so we can debug more easily
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef union selector_error_code {
    struct {
        uint32_t e : 1;
        uint32_t tbl : 2;
        uint32_t index : 13;
    };
    uint32_t packed;
} PACKED selector_error_code_t;

typedef struct exception_frame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rax;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    // SSP captured by the entry thunk via rdsspq, before any CALL could have
    // changed it. Zero if shadow stacks are disabled at the fault's CPL.
    uint64_t ssp;
    uint64_t _ssp_pad;
    uint64_t exception;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} exception_frame_t;

/**
 * Pretty print exception names
 */
static const char* const m_exception_names[] = {
    "#DE - Division Error",
    "#DB - Debug",
    "Non-maskable Interrupt",
    "#BP - Breakpoint",
    "#OF - Overflow",
    "#BR - Bound Range Exceeded",
    "#UD - Invalid Opcode",
    "#NM - Device Not Available",
    "#DF - Double Fault",
    "Coprocessor Segment Overrun",
    "#TS - Invalid TSS",
    "#NP - Segment Not Present",
    "#SS - Stack-Segment Fault",
    "#GP - General Protection Fault",
    "#PF - Page Fault",
    "Reserved",
    "#MF - x87 Floating-Point Exception",
    "#AC - Alignment Check",
    "#MC - Machine Check",
    "#XM/#XF - SIMD Floating-Point Exception",
    "#VE - Virtualization Exception",
    "#CP - Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "#HV - Hypervisor Injection Exception",
    "#VC - VMM Communication Exception",
    "#SX - Security Exception",
    "Reserved",
};
STATIC_ASSERT(ARRAY_LENGTH(m_exception_names) == 32);

static spinlock_t m_exception_lock = SPINLOCK_INIT;

static const char* get_segment_name(uint64_t segment) {
    switch (segment & ~0b111) {
        case GDT_KERNEL_CODE: return "kernel-code";
        case GDT_USER_CODE: return "user-code";
        case GDT_KERNEL_DATA: return "kernel-data";
        case GDT_USER_DATA: return "user-data";
        default: return "<unknown>";
    }
}

static const char* cp_error_description(uint32_t type) {
    switch (type) {
        case IA32_CP_EC_NEAR_RET:
            return "near RET return address did not match the shadow-stack copy";
        case IA32_CP_EC_FAR_RET_IRET:
            return "far RET / IRET return address did not match the shadow-stack copy";
        case IA32_CP_EC_ENDBRANCH:
            return "indirect branch target did not begin with ENDBR (IBT)";
        case IA32_CP_EC_RSTORSSP:
            return "RSTORSSP encountered an invalid shadow-stack restore token";
        case IA32_CP_EC_SETSSBSY:
            return "SETSSBSY encountered an invalid supervisor shadow-stack token";
        default:
            return "<unknown control-protection violation>";
    }
}

static bool cp_is_shadow_stack_fault(uint32_t type) {
    return type == IA32_CP_EC_NEAR_RET
        || type == IA32_CP_EC_FAR_RET_IRET
        || type == IA32_CP_EC_RSTORSSP
        || type == IA32_CP_EC_SETSSBSY;
}

// Map an x86_64 register index (0-15, with REX extensions already applied) to
// the matching saved value in the exception frame.
static uint64_t gpr_from_frame(exception_frame_t* frame, uint8_t reg) {
    switch (reg & 0xF) {
        case 0:  return frame->rax;
        case 1:  return frame->rcx;
        case 2:  return frame->rdx;
        case 3:  return frame->rbx;
        case 4:  return frame->rsp;
        case 5:  return frame->rbp;
        case 6:  return frame->rsi;
        case 7:  return frame->rdi;
        case 8:  return frame->r8;
        case 9:  return frame->r9;
        case 10: return frame->r10;
        case 11: return frame->r11;
        case 12: return frame->r12;
        case 13: return frame->r13;
        case 14: return frame->r14;
        case 15: return frame->r15;
    }
    return 0;
}

// Decode the memory operand of a `rstorssp (%reg)` at frame->rip. We only
// ever emit RSTORSSP in this form (see thread.S), so the encoding is fixed:
//
//   F3 [REX] 0F 01 /5  ModR/M(mod=00, reg=5, rm=reg)
//
// with rm != 4 (which would introduce a SIB byte) and rm != 5 (which would
// mean [rip+disp32]). Returns true on success with the effective address in
// *out; returns false otherwise.
static bool decode_rstorssp_operand(exception_frame_t* frame, uint64_t* out) {
    const uint8_t* p = (const uint8_t*)frame->rip;
    uint8_t rex = 0;

    if (*p != 0xF3) return false;
    p++;

    if ((*p & 0xF0) == 0x40) {
        rex = *p++;
    }

    if (p[0] != 0x0F || p[1] != 0x01) return false;
    p += 2;

    uint8_t modrm = *p;
    uint8_t mod = (modrm >> 6) & 3;
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t rm  = modrm & 7;

    if (reg != 5) return false;            // /5 extension must match RSTORSSP
    if (mod != 0 || rm == 4 || rm == 5) {  // only the plain [r64] form
        return false;
    }

    *out = gpr_from_frame(frame, rm | ((rex & 0x1) ? 8 : 0));
    return true;
}

// Recover the shadow stack pointer that held the return state at the moment of
// the fault, or 0 if unavailable.

// Recover the shadow stack pointer that held the return state at the moment of
// the fault, or 0 if unavailable.
//  - From user-mode: the hardware saved the user SSP into PL3_SSP on entry.
//  - From kernel-mode via IST: the CPU pushed a "restore token" with the
//    previous kernel SSP at the top of the new IST shadow stack. The entry
//    thunk captured that SSP into frame->ssp before any CALL could change it,
//    so we only need to deref the token and strip its low flag bits.
static uint64_t get_faulting_ssp(exception_frame_t* frame) {
    if ((frame->cs & 0b11) == 3) {
        return __rdmsr(MSR_IA32_PL3_SSP);
    }
    if (frame->ssp == 0) {
        return 0;
    }
    uint64_t token = *(volatile uint64_t*)frame->ssp;
    return token & ~0x7UL;
}

static void control_protection_dump(exception_frame_t* frame) {
    uint32_t ec = (uint32_t)frame->error_code;
    uint32_t type = ec & IA32_CP_EC_TYPE_MASK;

    ERROR("%s", cp_error_description(type));

    if (!cp_is_shadow_stack_fault(type)) {
        return;
    }

    uint64_t pl0_ssp = __rdmsr(MSR_IA32_PL0_SSP);
    uint64_t ssp = get_faulting_ssp(frame);

    switch (type) {
        case IA32_CP_EC_NEAR_RET: {
            // RSP still points at the return address -- RET hasn't popped yet.
            uint64_t got = *(volatile uint64_t*)frame->rsp;
            ERROR("got      return target @ [RSP=%016lx] = %016lx", frame->rsp, got);
            uint64_t expected = *(volatile uint64_t*)ssp;
            ERROR("expected return target @ [SSP=%016lx] = %016lx", ssp, expected);
        } break;

        case IA32_CP_EC_FAR_RET_IRET: {
            // Normal stack layout at fault: [RSP]=RIP, [RSP+8]=CS (and for a cross-
            // privilege IRET, RFLAGS/RSP/SS follow, but the CPU compares RIP+CS).
            // Shadow stack layout: [SSP]=LIP, [SSP+8]=CS, [SSP+16]=previous SSP.
            uint64_t stack_rip = *(volatile uint64_t*)frame->rsp;
            uint64_t stack_cs  = *(volatile uint64_t*)(frame->rsp + 8);
            ERROR("got      @ [RSP=%016lx]: RIP=%016lx CS=%04lx", frame->rsp, stack_rip, stack_cs & 0xFFFF);
            uint64_t shadow_lip      = *(volatile uint64_t*)ssp;
            uint64_t shadow_cs       = *(volatile uint64_t*)(ssp + 8);
            uint64_t shadow_prev_ssp = *(volatile uint64_t*)(ssp + 16);
            ERROR("expected @ [SSP=%016lx]: LIP=%016lx CS=%04lx prev_SSP=%016lx",
                ssp, shadow_lip, shadow_cs & 0xFFFF, shadow_prev_ssp);
            if (shadow_lip != stack_rip) {
                ERROR("  -> RIP mismatch (normal stack vs shadow stack LIP)");
            }
            if ((shadow_cs & 0xFFFF) != (stack_cs & 0xFFFF)) {
                ERROR("  -> CS mismatch (normal stack vs shadow stack CS)");
            }
        } break;

        case IA32_CP_EC_RSTORSSP: {
            ERROR("RSTORSSP at RIP=%016lx loaded an invalid shadow-stack restore token",
                frame->rip);
            uint64_t operand = 0;
            if (!decode_rstorssp_operand(frame, &operand)) {
                ERROR("could not decode the instruction operand at RIP");
                break;
            }
            uint64_t token = *(volatile uint64_t*)operand;
            ERROR("got      token @ [%016lx] = %016lx", operand, token);
            ERROR("expected token = %016lx", (operand & ~0x7) | BIT0);
            ERROR("actual   token = %016lx", token);
            if ((token & 1) == 0) {
                ERROR("  -> bit 0 (valid marker) is clear");
            }
            if (token & 2) {
                ERROR("  -> bit 1 (must-be-zero) is set");
            }
            if ((token & ~0x7UL) != operand) {
                ERROR("  -> token address field does not point back to the operand");
            }
        } break;

        case IA32_CP_EC_SETSSBSY: {
            // SETSSBSY reads a supervisor-shadow-stack token from [PL0_SSP] and
            // requires: bit 0 = 0 (not yet busy), bits[63:3] == PL0_SSP.
            uint64_t token = *(volatile uint64_t*)pl0_ssp;
            ERROR("got      token @ [PL0_SSP=%016lx] = %016lx", pl0_ssp, token);
            ERROR("expected token.busy = 0 AND token.addr(bits[63:3]) == PL0_SSP");
            ERROR("actual   token.busy = %d, token.addr = %016lx",
                (int)(token & 1), token & ~0x7UL);
            if (token & 1) {
                ERROR("  -> token already marked busy");
            }
            if ((token & ~0x7UL) != pl0_ssp) {
                ERROR("  -> token address does not point back to PL0_SSP");
            }
        } break;
    }
}

static void exception_dump_frame(exception_frame_t* frame) {

    // reset the spinlock so we can print
    ERROR("");
    ERROR("**************************************************************");
    ERROR("Exception occurred: %s (%lu)", m_exception_names[frame->exception], frame->exception);
    ERROR("**************************************************************");
    ERROR("");

    if (frame->exception == EXCEPT_IA32_PAGE_FAULT) {
        const char* prot = (frame->error_code & IA32_PF_EC_PROT) ? "protection fault" : "no page found";
        const char* access = (frame->error_code & IA32_PF_EC_WRITE) ? "write access" : "read access";
        const char* user = (frame->error_code & IA32_PF_EC_USER) ? "user-mode access" : "kernel-mode access";
        ERROR("page fault: %s, %s, %s", prot, access, user);

        if (frame->error_code & IA32_PF_EC_RSVD) ERROR("page fault: use of reserved bit detected");
        if (frame->error_code & IA32_PF_EC_INSTR) ERROR("page fault: fault was an instruction fetch");
        if (frame->error_code & IA32_PF_EC_PK) ERROR("page fault: protections keys block access");
        if (frame->error_code & IA32_PF_EC_SHSTK) ERROR("page fault: shadow stack access fault");
        if (frame->error_code & IA32_PF_EC_SGX) ERROR("page fault: SGX MMU page-fault");
        ERROR("");
    } else if (frame->exception == EXCEPT_IA32_GP_FAULT && frame->error_code != 0) {
        selector_error_code_t selector = (selector_error_code_t) { .packed = frame->error_code };
        static const char* const table[] = {
            "GDT",
            "IDT",
            "LDT",
            "IDT"
        };
        ERROR("Accessing %s[%d]", table[selector.tbl], selector.index);
        ERROR("");
    } else if (frame->exception == EXCEPT_IA32_CONTROL_PROTECTION) {
        control_protection_dump(frame);
        ERROR("");
    }

    // check if we have threading_old already
    ERROR("CPU: #%d", get_cpu_id());
    ERROR("");

    // registers
    rflags_t rflags = { .packed = frame->rflags };
    ERROR("RAX=%016lx RBX=%016lx RCX=%016lx RDX=%016lx", frame->rax, frame->rbx, frame->rcx, frame->rdx);
    ERROR("RSI=%016lx RDI=%016lx RBP=%016lx RSP=%016lx", frame->rsi, frame->rdi, frame->rbp, frame->rsp);
    ERROR("R8 =%016lx R9 =%016lx R10=%016lx R11=%016lx", frame->r8, frame->r9, frame->r10, frame->r11);
    ERROR("R12=%016lx R13=%016lx R14=%016lx R15=%016lx", frame->r12, frame->r13, frame->r14, frame->r15);
    ERROR("RIP=%016lx RFL=%08lx [%c%c%c%c%c%c%c]", frame->rip, rflags.packed,
            rflags.DF ? 'D' : '-',
            rflags.OF ? 'O' : '-',
            rflags.SF ? 'S' : '-',
            rflags.ZF ? 'Z' : '-',
            rflags.AC ? 'A' : '-',
            rflags.PF ? 'P' : '-',
            rflags.CF ? 'C' : '-'
    );
    ERROR("CS =%04lx DPL=%ld [%s]", frame->cs, frame->cs & 0b111, get_segment_name(frame->cs));
    ERROR("SS =%04lx DPL=%ld [%s]", frame->ss, frame->ss & 0b111, get_segment_name(frame->ss));
    ERROR("FS =%016lx", __rdmsr(MSR_IA32_FS_BASE));
    ERROR("GS =%016lx", __rdmsr(MSR_IA32_KERNEL_GS_BASE));
    ERROR("CR0=%08lx CR2=%016lx CR3=%016lx CR4=%08lx", __readcr0(), __readcr2(), __readcr3(), __readcr4());

    if (g_shadow_stack_supported) {
        uint64_t pl0_ssp = __rdmsr(MSR_IA32_PL0_SSP);
        uint64_t pl3_ssp = __rdmsr(MSR_IA32_PL3_SSP);
        uint64_t ist_ssp = __rdmsr(MSR_IA32_INTERRUPT_SSP_TABLE_ADDR);
        ERROR("PL0_SSP=%016lx PL3_SSP=%016lx INT_SSP_TABLE=%016lx", pl0_ssp, pl3_ssp, ist_ssp);
        ERROR("SSP=%016lx", get_faulting_ssp(frame));
    }
}

/**
 * The default exception handler, simply panics...
 */
noreturn static void panic_exception_handler(exception_frame_t* frame) {
    irq_enable();
    spinlock_acquire(&m_exception_lock);
    lapic_send_ipi_all_excluding_self(INTR_VECTOR_PANIC);
    spinlock_release(&m_exception_lock);

    // allow to access usermode for fun and profit
    user_access_enable();
    exception_dump_frame(frame);

    // stop
    ERROR("");
    ERROR("Halting :(");
    while (1)
        asm("hlt");
}

/**
 * Swap the GS base if coming from usermode, otherwise leave as is
 */
static void swapgs(exception_frame_t* frame) {
    if ((frame->cs & 0b11) == 3) {
        asm("swapgs");
    }
}

static bool page_fault_handler(exception_frame_t* frame) {
    irq_enable();
    bool success = !IS_ERROR(virt_handle_page_fault(__readcr2(), frame->error_code));
    irq_disable();
    return success;
}

__attribute__((used))
OMIT_ENDBR void exception_common_handler(exception_frame_t* frame) {
    swapgs(frame);

    if (frame->exception == EXCEPT_IA32_PAGE_FAULT) {
        // try to handle the page fault first
        if (page_fault_handler(frame)) {
            swapgs(frame);
            return;
        }
    } else if (frame->exception == EXCEPT_IA32_DEBUG) {
        exception_dump_frame(frame);
        swapgs(frame);
        return;
    }

    // was not able to handle it, panic
    panic_exception_handler(frame);
}

#define EXCEPTION_STUB(num) \
    __attribute__((naked)) \
    static void exception_handler_##num(void) { \
        asm( \
            "pushq $0\n" \
            "pushq $" #num "\n" \
            "jmp exception_common_thunk\n" \
        ); \
    }

#define EXCEPTION_ERROR_STUB(num) \
    __attribute__((naked)) \
    static void exception_handler_##num(void) { \
        asm( \
            "pushq $" #num "\n" \
            "jmp exception_common_thunk\n" \
        ); \
    }

EXCEPTION_STUB(0x00);
EXCEPTION_STUB(0x01);
EXCEPTION_STUB(0x02);
EXCEPTION_STUB(0x03);
EXCEPTION_STUB(0x04);
EXCEPTION_STUB(0x05);
EXCEPTION_STUB(0x06);
EXCEPTION_STUB(0x07);
EXCEPTION_ERROR_STUB(0x08);
EXCEPTION_STUB(0x09);
EXCEPTION_ERROR_STUB(0x0A);
EXCEPTION_ERROR_STUB(0x0B);
EXCEPTION_ERROR_STUB(0x0C);
EXCEPTION_ERROR_STUB(0x0D);
EXCEPTION_ERROR_STUB(0x0E);
EXCEPTION_STUB(0x0F);
EXCEPTION_STUB(0x10);
EXCEPTION_ERROR_STUB(0x11);
EXCEPTION_STUB(0x12);
EXCEPTION_STUB(0x13);
EXCEPTION_STUB(0x14);
EXCEPTION_ERROR_STUB(0x15);
EXCEPTION_STUB(0x16);
EXCEPTION_STUB(0x17);
EXCEPTION_STUB(0x18);
EXCEPTION_STUB(0x19);
EXCEPTION_STUB(0x1A);
EXCEPTION_STUB(0x1B);
EXCEPTION_STUB(0x1C);
EXCEPTION_ERROR_STUB(0x1D);
EXCEPTION_ERROR_STUB(0x1E);
EXCEPTION_STUB(0x1F);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scheduler interrupt
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

__attribute__((interrupt))
static void ipi_interrupt_handler(interrupt_frame_t* frame) {
    ipi_handle();
    lapic_eoi();
}

__attribute__((interrupt))
static void panic_handler(interrupt_frame_t* frame) {
    while (true) {
        asm("hlt");
    }
}

__attribute__((interrupt))
static void spurious_interrupt_handler(interrupt_frame_t* frame) {
    ASSERT(!"Got spurious interrupt?");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////but it
// IDT setup
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * All interrupt handler entries
 */
LATE_RO static idt_entry_t m_idt_entries[256];

/**
 * Set a single idt entry
 */
INIT_CODE static void intr_set_kernel_handler(uint8_t vector, void* func) {
    m_idt_entries[vector].handler_low = (uint16_t) ((uintptr_t)func & 0xFFFF);
    m_idt_entries[vector].handler_high = (uint64_t) ((uintptr_t)func >> 16);
    m_idt_entries[vector].gate_type = IDT_TYPE_INTERRUPT_32;
    m_idt_entries[vector].selector = GDT_KERNEL_CODE;
    m_idt_entries[vector].present = 1;
    m_idt_entries[vector].ist = 0;
}

INIT_CODE void init_idt(void) {
    // generic exception handlers
    intr_set_kernel_handler(EXCEPT_IA32_DIVIDE_ERROR, exception_handler_0x00);
    intr_set_kernel_handler(EXCEPT_IA32_DEBUG, exception_handler_0x01);
    intr_set_kernel_handler(EXCEPT_IA32_NMI, exception_handler_0x02);
    intr_set_kernel_handler(EXCEPT_IA32_BREAKPOINT, exception_handler_0x03);
    intr_set_kernel_handler(EXCEPT_IA32_OVERFLOW, exception_handler_0x04);
    intr_set_kernel_handler(EXCEPT_IA32_BOUND, exception_handler_0x05);
    intr_set_kernel_handler(EXCEPT_IA32_INVALID_OPCODE, exception_handler_0x06);
    intr_set_kernel_handler(0x07, exception_handler_0x07);
    intr_set_kernel_handler(EXCEPT_IA32_DOUBLE_FAULT, exception_handler_0x08);
    intr_set_kernel_handler(0x09, exception_handler_0x09);
    intr_set_kernel_handler(EXCEPT_IA32_INVALID_TSS, exception_handler_0x0A);
    intr_set_kernel_handler(EXCEPT_IA32_SEG_NOT_PRESENT, exception_handler_0x0B);
    intr_set_kernel_handler(EXCEPT_IA32_STACK_FAULT, exception_handler_0x0C);
    intr_set_kernel_handler(EXCEPT_IA32_GP_FAULT, exception_handler_0x0D);
    intr_set_kernel_handler(EXCEPT_IA32_PAGE_FAULT, exception_handler_0x0E);
    intr_set_kernel_handler(0x0F, exception_handler_0x0F);
    intr_set_kernel_handler(EXCEPT_IA32_FP_ERROR, exception_handler_0x10);
    intr_set_kernel_handler(EXCEPT_IA32_ALIGNMENT_CHECK, exception_handler_0x11);
    intr_set_kernel_handler(EXCEPT_IA32_MACHINE_CHECK, exception_handler_0x12);
    intr_set_kernel_handler(EXCEPT_IA32_SIMD, exception_handler_0x13);
    intr_set_kernel_handler(0x14, exception_handler_0x14);
    intr_set_kernel_handler(0x15, exception_handler_0x15);
    intr_set_kernel_handler(0x16, exception_handler_0x16);
    intr_set_kernel_handler(0x17, exception_handler_0x17);
    intr_set_kernel_handler(0x18, exception_handler_0x18);
    intr_set_kernel_handler(0x19, exception_handler_0x19);
    intr_set_kernel_handler(0x1A, exception_handler_0x1A);
    intr_set_kernel_handler(0x1B, exception_handler_0x1B);
    intr_set_kernel_handler(0x1C, exception_handler_0x1C);
    intr_set_kernel_handler(0x1D, exception_handler_0x1D);
    intr_set_kernel_handler(0x1E, exception_handler_0x1E);
    intr_set_kernel_handler(0x1F, exception_handler_0x1F);

    // handlers with specific behaviour
    intr_set_kernel_handler(INTR_VECTOR_TIMER, timer_interrupt_handler);
    intr_set_kernel_handler(INTR_VECTOR_IPI, ipi_interrupt_handler);
    intr_set_kernel_handler(INTR_VECTOR_PANIC, panic_handler);
    intr_set_kernel_handler(INTR_VECTOR_SPURIOUS, spurious_interrupt_handler);

    // allow from usermode
    m_idt_entries[EXCEPT_IA32_BREAKPOINT].ring = 3;

    idt_t idt = {
        .limit = sizeof(m_idt_entries) - 1,
        .base = m_idt_entries
    };
    asm volatile ("lidt %0" : : "m" (idt));
}

INIT_CODE void init_idt_stacks(void) {
    m_idt_entries[EXCEPT_IA32_DOUBLE_FAULT].ist = TSS_IST_DF + 1;
    m_idt_entries[EXCEPT_IA32_NMI].ist = TSS_IST_NMI + 1;
    m_idt_entries[EXCEPT_IA32_BREAKPOINT].ist = TSS_IST_DB + 1;
    m_idt_entries[EXCEPT_IA32_DEBUG].ist = TSS_IST_DB + 1;
    m_idt_entries[EXCEPT_IA32_MACHINE_CHECK].ist = TSS_IST_MCE + 1;
    m_idt_entries[EXCEPT_IA32_CONTROL_PROTECTION].ist = TSS_IST_CP + 1;
}
