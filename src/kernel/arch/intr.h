#pragma once

#include "arch/regs.h"
#include "lib/defs.h"
#include "uapi/syscall.h"

#define EXCEPT_IA32_DIVIDE_ERROR     0
#define EXCEPT_IA32_DEBUG            1
#define EXCEPT_IA32_NMI              2
#define EXCEPT_IA32_BREAKPOINT       3
#define EXCEPT_IA32_OVERFLOW         4
#define EXCEPT_IA32_BOUND            5
#define EXCEPT_IA32_INVALID_OPCODE   6
#define EXCEPT_IA32_DOUBLE_FAULT     8
#define EXCEPT_IA32_INVALID_TSS      10
#define EXCEPT_IA32_SEG_NOT_PRESENT  11
#define EXCEPT_IA32_STACK_FAULT      12
#define EXCEPT_IA32_GP_FAULT         13
#define EXCEPT_IA32_PAGE_FAULT       14
#define EXCEPT_IA32_FP_ERROR         16
#define EXCEPT_IA32_ALIGNMENT_CHECK  17
#define EXCEPT_IA32_MACHINE_CHECK    18
#define EXCEPT_IA32_SIMD             19

#define IA32_PF_EC_PROT     BIT0
#define IA32_PF_EC_WRITE    BIT1
#define IA32_PF_EC_USER     BIT2
#define IA32_PF_EC_RSVD     BIT3
#define IA32_PF_EC_INSTR    BIT4
#define IA32_PF_EC_PK       BIT5
#define IA32_PF_EC_SHSTK    BIT6
#define IA32_PF_EC_SGX      BIT15
#define IA32_PF_EC_RMP      BIT31

#define INTR_VECTOR_IPI         0x20
#define INTR_VECTOR_SPURIOUS    0x21
#define INTR_VECTOR_TIMER       0x22

INIT_CODE void init_idt(void);

INIT_CODE void intr_set_user_handler(uint8_t vector, interrupt_handler_t handler);
