.section .text

.global syscall_entry
syscall_entry:
    endbr64

    /**
     * Ensure we have the kernel GS base now
     */
    swapgs

    /**
     * Stash the user stack in a per-cpu scratch slot and
     * switch to the kernel stack.
     */
    movq %rsp, %gs:m_user_rsp_scratch
    movq %gs:m_tss+4, %rsp

    /**
     * Save only the caller-saved registers.
     */
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11

    /**
     * Call the kernel handler
     */
    movq %rsp, %rdi
    call syscall_handler

    /**
     * Restore the caller-saved registers
     */
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rax

    /**
     * Restore the user stack pointer from the per-cpu
     * scratch slot.
     */
    movq %gs:m_user_rsp_scratch, %rsp

    /**
     * Restore the user gsbase
     */
    swapgs

    /**
     * And finally return to usermode
     */
    sysretq
