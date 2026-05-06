.section .text

.global syscall_entry
.type syscall_entry, @function
syscall_entry:
.cfi_startproc
    .cfi_signal_frame
    /**
     * On syscall entry the CPU has not pushed any frame: %rsp is still
     * the user stack pointer, %rcx holds the user RIP we should return
     * to, and %r11 holds the user RFLAGS. Anchor the CFA at the user
     * RSP and tell the unwinder where to find the user RIP/RFLAGS so a
     * stack trace through this trampoline can reach the userspace
     * caller. The default rule (previous rsp = CFA) recovers user RSP.
     */
    .cfi_def_cfa rsp, 0
    .cfi_register rip, rcx
    .cfi_register rflags, r11

    endbr64

    /**
     * Ensure we have the kernel GS base now
     */
    swapgs

    /**
     * The syscall is allowed to touch rax because
     * it is only used by the result, so we are using
     * it to restore the user ssp while we are setting
     * up a kernel ssp. once setup we are going to store
     * it on the stack
     */
    movq %rsp, %rax
    /* The CFA (= user RSP value) is now also held in %rax. */
    .cfi_def_cfa rax, 0
    movq %gs:m_tss+4, %rsp
    pushq %rax
    /**
     * The user RSP value is now saved at the top of the kernel stack.
     * Re-anchor the CFA to the kernel stack: from here on it stays at
     * the same kernel address regardless of further pushes/pops, and
     * the user RSP can be recovered from the slot we just pushed.
     */
    .cfi_def_cfa rsp, 8
    .cfi_offset rsp, -8

    /**
     * save regs that have to be restored, only from the
     * caller saved list, note we don't need to save rax
     * because it is always clobbered by the fact
     */
    pushq %rcx
    .cfi_adjust_cfa_offset 8
    .cfi_rel_offset rip, 0
    pushq %rdx
    .cfi_adjust_cfa_offset 8
    .cfi_rel_offset rdx, 0
    pushq %rsi
    .cfi_adjust_cfa_offset 8
    .cfi_rel_offset rsi, 0
    pushq %rdi
    .cfi_adjust_cfa_offset 8
    .cfi_rel_offset rdi, 0
    pushq %r8
    .cfi_adjust_cfa_offset 8
    .cfi_rel_offset r8, 0
    pushq %r9
    .cfi_adjust_cfa_offset 8
    .cfi_rel_offset r9, 0
    pushq %r10
    .cfi_adjust_cfa_offset 8
    .cfi_rel_offset r10, 0
    pushq %r11
    .cfi_adjust_cfa_offset 8
    .cfi_rel_offset rflags, 0

    /**
     * Reserve a slot for the kernel shadow stack pointer. We always
     * reserve it (even when shadow stacks are not supported) so the
     * CFI is uniform across both code paths through the function.
     */
    subq $8, %rsp
    .cfi_adjust_cfa_offset 8

    /**
     * If shadow stacks are supported, enable the kernel shadow stack
     * and stash its SSP in the slot we just reserved so we can clear
     * it later.
     */
    cmpb $0, g_shadow_stack_supported(%rip)
    je 1f
    setssbsy
    rdsspq %rax
    movq %rax, (%rsp)
1:

    /*
     * interrupts can now be enabled
     */
    sti

    /**
     * finally call the handler
     */
    call syscall_handler

    /*
     * disable interrupts as the first
     * thing we do
     */
    cli

    /**
     * If shadow stacks are supported, clear the kernel shadow stack
     * using the SSP we stashed in the reserved slot.
     */
    cmpb $0, g_shadow_stack_supported(%rip)
    je 2f
    movq (%rsp), %rdi
    clrssbsy (%rdi)
2:
    addq $8, %rsp
    .cfi_adjust_cfa_offset -8

    /**
     * Restore the caller-saved registers, the other regs
     * that were modified are restored by the syscall handler
     * itself. rax will contain the result at this point
     */
    popq %r11
    .cfi_adjust_cfa_offset -8
    .cfi_register rflags, r11
    popq %r10
    .cfi_adjust_cfa_offset -8
    .cfi_restore r10
    popq %r9
    .cfi_adjust_cfa_offset -8
    .cfi_restore r9
    popq %r8
    .cfi_adjust_cfa_offset -8
    .cfi_restore r8
    popq %rdi
    .cfi_adjust_cfa_offset -8
    .cfi_restore rdi
    popq %rsi
    .cfi_adjust_cfa_offset -8
    .cfi_restore rsi
    popq %rdx
    .cfi_adjust_cfa_offset -8
    .cfi_restore rdx
    popq %rcx
    .cfi_adjust_cfa_offset -8
    .cfi_register rip, rcx

    /**
     * Restore the user stack pointer from the
     * stack because we pushed it before
     */
    popq %rsp
    /* We are back on the user stack; re-anchor CFA to it. */
    .cfi_def_cfa rsp, 0
    .cfi_restore rsp

    /**
     * Restore the user gsbase
     */
    swapgs

    /**
     * And finally return to usermode
     */
    sysretq
.cfi_endproc
.size syscall_entry, . - syscall_entry
