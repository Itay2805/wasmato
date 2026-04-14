#include "intr.h"

#include <stdnoreturn.h>

#include "apic.h"
#include "gdt.h"
#include "lib/ipi.h"
#include "lib/log.h"
#include "mem/virt.h"
#include "sync/spinlock.h"
#include "lib/pcpu.h"
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
        static const char* const table[] = {
            "NEAR-RET",
            "FAR-RET/IRET",
            "ENDBRANCH",
            "RSTORSSP",
            "SETSSBSY"
        };
        uint32_t cpec = frame->error_code & 0x7fff;
        if (cpec != 0 && cpec <= ARRAY_LENGTH(table)) {
            ERROR("From %s", table[cpec - 1]);
        } else {
            ERROR("From %x", cpec);
        }
        if ((frame->error_code & BIT15) != 0) {
            ERROR("Inside enclave");
        }

        if (cpec == 1 || cpec == 2 || cpec == 4 || cpec == 5) {
            uint64_t* shadow_stack = (uint64_t*)__rdmsr(MSR_IA32_PL3_SSP);
            uint64_t* stack = (uint64_t*)frame->rsp;
            ERROR("\tSSP:      %016lx", (uintptr_t)shadow_stack);
            if (cpec == 1) {
                ERROR("\tExpected: %016lx", shadow_stack[-1]);
                ERROR("\tGot:      %016lx", stack[0]);
            }
        }
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
}

/**
 * The default exception handler, simply panics...
 */
noreturn static void panic_exception_handler(exception_frame_t* frame) {
    ipi_enable();
    spinlock_acquire(&m_exception_lock);
    lapic_send_ipi_all_excluding_self(INTR_VECTOR_PANIC);
    spinlock_release(&m_exception_lock);

    // allow to access usermode for fun and profit
    user_access_enable();
    exception_dump_frame(frame);

    // stop
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
    ipi_enable();
    bool success = !IS_ERROR(virt_handle_page_fault(__readcr2(), frame->error_code));
    ipi_disable();
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
INIT_CODE static void intr_set_kernel_handler(uint8_t vector, void* func, int ist, int ring) {
    m_idt_entries[vector].handler_low = (uint16_t) ((uintptr_t)func & 0xFFFF);
    m_idt_entries[vector].handler_high = (uint64_t) ((uintptr_t)func >> 16);
    m_idt_entries[vector].gate_type = IDT_TYPE_INTERRUPT_32;
    m_idt_entries[vector].selector = GDT_KERNEL_CODE;
    m_idt_entries[vector].present = 1;
    m_idt_entries[vector].ring = ring;
    m_idt_entries[vector].ist = ist + 1;
}

INIT_CODE void init_idt(void) {
    // generic exception handlers
    intr_set_kernel_handler(EXCEPT_IA32_DIVIDE_ERROR, exception_handler_0x00, -1, 0);
    intr_set_kernel_handler(EXCEPT_IA32_DEBUG, exception_handler_0x01, TSS_IST_DB, 3);
    intr_set_kernel_handler(EXCEPT_IA32_NMI, exception_handler_0x02, TSS_IST_NMI, 0);
    intr_set_kernel_handler(EXCEPT_IA32_BREAKPOINT, exception_handler_0x03, TSS_IST_DB, 3);
    intr_set_kernel_handler(EXCEPT_IA32_OVERFLOW, exception_handler_0x04, -1, 0);
    intr_set_kernel_handler(EXCEPT_IA32_BOUND, exception_handler_0x05, -1, 0);
    intr_set_kernel_handler(EXCEPT_IA32_INVALID_OPCODE, exception_handler_0x06, -1, 0);
    intr_set_kernel_handler(0x07, exception_handler_0x07, -1, 0);
    intr_set_kernel_handler(EXCEPT_IA32_DOUBLE_FAULT, exception_handler_0x08, TSS_IST_DF, 0);
    intr_set_kernel_handler(0x09, exception_handler_0x09, -1, 0);
    intr_set_kernel_handler(EXCEPT_IA32_INVALID_TSS, exception_handler_0x0A, -1, 0);
    intr_set_kernel_handler(EXCEPT_IA32_SEG_NOT_PRESENT, exception_handler_0x0B, -1, 0);
    intr_set_kernel_handler(EXCEPT_IA32_STACK_FAULT, exception_handler_0x0C, -1, 0);
    intr_set_kernel_handler(EXCEPT_IA32_GP_FAULT, exception_handler_0x0D, -1, 0);
    intr_set_kernel_handler(EXCEPT_IA32_PAGE_FAULT, exception_handler_0x0E, -1, 0);
    intr_set_kernel_handler(0x0F, exception_handler_0x0F, -1, 0);
    intr_set_kernel_handler(EXCEPT_IA32_FP_ERROR, exception_handler_0x10, -1, 0);
    intr_set_kernel_handler(EXCEPT_IA32_ALIGNMENT_CHECK, exception_handler_0x11, -1, 0);
    intr_set_kernel_handler(EXCEPT_IA32_MACHINE_CHECK, exception_handler_0x12, TSS_IST_MCE, 0);
    intr_set_kernel_handler(EXCEPT_IA32_SIMD, exception_handler_0x13, -1, 0);
    intr_set_kernel_handler(0x14, exception_handler_0x14, -1, 0);
    intr_set_kernel_handler(0x15, exception_handler_0x15, -1, 0);
    intr_set_kernel_handler(0x16, exception_handler_0x16, -1, 0);
    intr_set_kernel_handler(0x17, exception_handler_0x17, -1, 0);
    intr_set_kernel_handler(0x18, exception_handler_0x18, -1, 0);
    intr_set_kernel_handler(0x19, exception_handler_0x19, -1, 0);
    intr_set_kernel_handler(0x1A, exception_handler_0x1A, -1, 0);
    intr_set_kernel_handler(0x1B, exception_handler_0x1B, -1, 0);
    intr_set_kernel_handler(0x1C, exception_handler_0x1C, -1, 0);
    intr_set_kernel_handler(0x1D, exception_handler_0x1D, -1, 0);
    intr_set_kernel_handler(0x1E, exception_handler_0x1E, -1, 0);
    intr_set_kernel_handler(0x1F, exception_handler_0x1F, -1, 0);

    // handlers with specific behaviour
    intr_set_kernel_handler(INTR_VECTOR_TIMER, timer_interrupt_handler, -1, 0);
    intr_set_kernel_handler(INTR_VECTOR_IPI, ipi_interrupt_handler, -1, 0);
    intr_set_kernel_handler(INTR_VECTOR_PANIC, panic_handler, -1, 0);
    intr_set_kernel_handler(INTR_VECTOR_SPURIOUS, spurious_interrupt_handler, -1, 0);

    idt_t idt = {
        .limit = sizeof(m_idt_entries) - 1,
        .base = m_idt_entries
    };
    asm volatile ("lidt %0" : : "m" (idt));
}
