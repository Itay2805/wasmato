.section .text

/**
 * void thread_do_switch(thread_t* from, thread_t* to);
 */
.global thread_do_switch
thread_do_switch:
    /*
     * save the current state, we save the rbp
     * first to have nice call frames
     */
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /*
     * push the flags and clear interrupts as we are about
     * to switch the stacks to ensure that nothing happens
     * before we restore the original flags (in-case of
     * interrupts being off at that time
     */
    pushfq
    cli

    /*
     * save the current stack into the from thread and
     * restore the stack of the incoming thread
     */
    movq %rsp, (%rdi)
    movq (%rsi), %rsp

    /*
     * restore the flags
     */
    popfq

    /*
     * restore the register state
     */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp

    /* finally restore the ip */
    ret

/**
 * void thread_do_jump(thread_t* to);
 */
.global thread_do_jump
thread_do_jump:
    /*
     * clear interrupts as we are about to switch the stacks
     to ensure that nothing happens before we restore the
     * original flags (in-case of interrupts being off at
     * that time)
     */
    cli

    /*
     * restore the stack
     */
    movq (%rdi), %rsp

    /*
     * restore the flags
     */
    popfq

    /*
     * restore the register state
     */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp

    /* finally restore the ip */
    ret

/**
 * void thread_do_jump_to_user(void* rip, void* stack, void* arg);
 */
.global thread_do_jump_to_user
thread_do_jump_to_user:
    /*
     * setup the user cotext after the sysret
     *  rcx -> rip
     *  r11 -> rflags (BIT1 | IF | IOPL(3) | ID)
     */
    movq %rdi, %rcx
    movq $0x203202, %r11

    /**
     * Disable interrupts, and switch to the usermode stack
     * the sysret will restore interrupts nicely
     */
    cli
    movq %rsi, %rsp

    /*
     * clear the other registers
     */
    movq $0, %rax
    movq %rax, %rbx
    movq %rax, %rdx
    movq %rax, %rsi
    movq %rax, %rbp
    movq %rax, %r8
    movq %rax, %r9
    movq %rax, %r10
    movq %rax, %r12
    movq %rax, %r13
    movq %rax, %r14
    movq %rax, %r15

    /**
     * and go to usermode
     */
    sysretq
