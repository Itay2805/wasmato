.section .text


/**
 * void do_thread_switch(thread_t* to, thread_t* from);
 */
.global do_thread_switch
do_thread_switch:
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
    movq %rsp, (%rsi)
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

    /* finally restore the ip */
    ret

/**
 * void do_thread_resume(thread_t* to);
 */
.global do_thread_resume
do_thread_resume:
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

    /* finally restore the ip */
    ret

/**
 * void thread_entry_thunk(void);
 */
.global thread_entry_thunk
thread_entry_thunk:
    // we assume rdi is whatever
    popq %rsi
    popq %rdx
    jmp thread_entry_point
