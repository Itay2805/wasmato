.section .text


/**
 * void do_thread_switch(thread_t* to, thread_t* from);
 */
.type exception_common_thunk, @function
.globl exception_common_thunk
exception_common_thunk:
.cfi_startproc
    .cfi_signal_frame
    .cfi_def_cfa rsp, 56
    .cfi_rel_offset ss, 48
    .cfi_rel_offset rsp, 40
    .cfi_rel_offset rflags, 32
    .cfi_rel_offset cs, 24
    .cfi_rel_offset rip, 16

    // clear direction
    cld

    // push all gprs
    pushq %rdi
    .cfi_rel_offset rdi, 0
    .cfi_adjust_cfa_offset 8

    pushq %rsi
    .cfi_rel_offset rsi, 0
    .cfi_adjust_cfa_offset 8

    pushq %rdx
    .cfi_rel_offset rdx, 0
    .cfi_adjust_cfa_offset 8

    pushq %rcx
    .cfi_rel_offset rcx, 0
    .cfi_adjust_cfa_offset 8

    pushq %rax
    .cfi_rel_offset rax, 0
    .cfi_adjust_cfa_offset 8

    pushq %r8
    .cfi_rel_offset r8, 0
    .cfi_adjust_cfa_offset 8

    pushq %r9
    .cfi_rel_offset r9, 0
    .cfi_adjust_cfa_offset 8

    pushq %r10
    .cfi_rel_offset r10, 0
    .cfi_adjust_cfa_offset 8

    pushq %r11
    .cfi_rel_offset r11, 0
    .cfi_adjust_cfa_offset 8

    pushq %rbx
    .cfi_rel_offset rbx, 0
    .cfi_adjust_cfa_offset 8

    pushq %rbp
    .cfi_rel_offset rbp, 0
    .cfi_adjust_cfa_offset 8

    pushq %r12
    .cfi_rel_offset r12, 0
    .cfi_adjust_cfa_offset 8

    pushq %r13
    .cfi_rel_offset r13, 0
    .cfi_adjust_cfa_offset 8

    pushq %r14
    .cfi_rel_offset r14, 0
    .cfi_adjust_cfa_offset 8

    pushq %r15
    .cfi_rel_offset r15, 0
    .cfi_adjust_cfa_offset 8

    movq %rsp, %rdi
    call exception_common_handler

    popq %r15
    .cfi_restore r15
    .cfi_adjust_cfa_offset -8

    popq %r14
    .cfi_restore r14
    .cfi_adjust_cfa_offset -8

    popq %r13
    .cfi_restore r13
    .cfi_adjust_cfa_offset -8

    popq %r12
    .cfi_restore r12
    .cfi_adjust_cfa_offset -8

    popq %rbp
    .cfi_restore rbp
    .cfi_adjust_cfa_offset -8

    popq %rbx
    .cfi_restore rbx
    .cfi_adjust_cfa_offset -8

    popq %r11
    .cfi_restore r11
    .cfi_adjust_cfa_offset -8

    popq %r10
    .cfi_restore r10
    .cfi_adjust_cfa_offset -8

    popq %r9
    .cfi_restore r9
    .cfi_adjust_cfa_offset -8

    popq %r8
    .cfi_restore r8
    .cfi_adjust_cfa_offset -8

    popq %rax
    .cfi_restore rax
    .cfi_adjust_cfa_offset -8

    popq %rcx
    .cfi_restore rcx
    .cfi_adjust_cfa_offset -8

    popq %rdx
    .cfi_restore rdx
    .cfi_adjust_cfa_offset -8

    popq %rsi
    .cfi_restore rsi
    .cfi_adjust_cfa_offset -8

    popq %rdi
    .cfi_restore rdi
    .cfi_adjust_cfa_offset -8

    addq $16, %rsp

    iretq
.cfi_endproc
.size exception_common_thunk, . - exception_common_thunk
