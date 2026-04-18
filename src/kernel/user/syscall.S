.section .text

.global syscall_entry
syscall_entry:
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
    movq %gs:m_tss+4, %rsp
    pushq %rax

    /**
     * save regs that have to be restored, only from the 
     * caller saved list, note we don't need to save rax
     * because it is always clobbered by the fact 
     */
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11

    /**
     * We enable the kernel shadow stack and store 
     * it aside so we can properly clear it later on
     * TODO: check if shadow stack is enabled or not
     */
    setssbsy
    rdsspq %rax
    pushq %rax

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
     * clear the shadow stack because we no
     * longer need it, we stashed on the stack
     * the shadow stack token
     */
    popq %rdi
    clrssbsy (%rdi)

    /**
     * Restore the caller-saved registers, the other regs
     * that were modified are restored by the syscall handler
     * itself. rax will contain the result at this point
     */
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx

    /**
     * Restore the user stack pointer from the 
     * stack because we pushed it before
     */
    popq %rsp

    /**
     * Restore the user gsbase
     */
    swapgs

    /**
     * And finally return to usermode
     */
    sysretq
