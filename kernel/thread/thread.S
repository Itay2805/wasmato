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
     * save the current stack into the from thread and
     * restore the stack of the incoming thread
     */
    movq %rsp, (%rdi)
    movq (%rsi), %rsp

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
     * restore the stack
     */
    movq (%rdi), %rsp

    /*
     * restore the register state
     */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp

    /* we assume threads always have their interrupts enabled */
    sti

    /* finally restore the ip */
    ret
