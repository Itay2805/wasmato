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
     * save the current stack into the from thread
     */
    movq %rsp, (%rsi)

    /**
     * Get the shadow stack before we do
     * any manipulations
     */
    // rdsspq %rax

    /**
     * Get the new shadow stack from the
     * next thread
     */
    // movq 8(%rdi), %rcx

    /**
     * Switch to the new shadow stack
     */
    // rstorssp (%rcx)
    // saveprevssp

    /**
     * Save the old shadow stack, we need
     * to decrease it by 8 to account for
     * the restore token that was pushed
     */
    // subq $8, %rax
    // movq %rax, 8(%rsi)

    /**
     * Finally switch to the new real stack
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
 * void do_thread_resume(thread_t* to);
 */
.global do_thread_resume
do_thread_resume:
    /*
     * Switch to the incoming thread's shadow stack.
     * We still call SAVEPREVSSP to satisfy the architectural
     * requirement that RSTORSSP is always paired with SAVEPREVSSP;
     * the token it writes for the abandoned shadow stack is harmless.
     */
    // movq 8(%rdi), %rax
    // rstorssp (%rax)
    // saveprevssp

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
