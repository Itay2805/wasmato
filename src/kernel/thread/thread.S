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
    rdsspq %rax

    /**
     * Get the new shadow stack from the
     * next thread
     */
    movq 8(%rdi), %rcx

    /**
     * Switch to the new shadow stack
     */
    rstorssp (%rcx)
    saveprevssp

    /**
     * Save the old shadow stack, we need
     * to decrease it by 8 to account for
     * the restore token that was pushed
     */
    subq $8, %rax
    movq %rax, 8(%rsi)

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
 * void thread_entry_thunk(void);
 */
.global thread_entry_thunk
thread_entry_thunk:
    // we assume rdi is whatever
    popq %rsi
    popq %rdx
    jmp thread_entry_point

.section .text.init

/**
 * void do_thread_bootstrap(thread_t* to);
 */
.global do_thread_bootstrap_shstk
do_thread_bootstrap_shstk:
    /**
     * The most import thing we need to do first
     * is to actually enable shadow stacks, we do
     * it in here to ensure we have no calls/rets
     * in between this and the switching into the
     * first shadow stack
     */
    movl $0x6A2, %ecx
    rdmsr
    orl $1, %eax
    wrmsr

    /*
     * Switch to the incoming thread's shadow stack.
     * We don't need the previous stack token so we are
     * going to just ignore ignore it and pop the
     * previous stack token
     */
    mov $1, %rcx
    movq 8(%rdi), %rax
    rstorssp (%rax)
    incsspq %rcx
    jnc 1f
    incsspd %ecx
1:

/**
 * void do_thread_bootstrap(thread_t* to);
 */
.global do_thread_bootstrap
do_thread_bootstrap:
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
