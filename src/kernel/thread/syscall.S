.section .text

.global syscall_entry
syscall_entry:
    /**
     * Save the stack pointer in memory
     * and switch to the kernel stack
     */
    xchg %rsp, %gs:g_syscall_stack

    /**
     * Save all the registers
     */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /**
     * Call the kernel handler
     */
    movq %rsp, %rdi
    call syscall_handler

    /**
     * Restore all the registers
     */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    /**
     * Restore the stack pointer
     */
    xchg %rsp, %gs:g_syscall_stack

    /**
     * And finally return to usermode
     */
    sysretq
