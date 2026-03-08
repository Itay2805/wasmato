.section .text.init

.global usermode_jump
usermode_jump:

    /**
     * Switch to the usermode GS
     */
    swapgs

    /**
     * switch to the user stack
     */
    movq %rdx, %rsp

    /**
     * set the sysretq args
     */
    movq %rsi, %rcx

    /**
     * Set rflags:
     * - IOPL = 3
     * - ID = 1
     */
    movq $0x203002, %r11

    /**
     * zero the rest of the regs
     * to not leak anything
     */
    xorq %rax, %rax
    xorq %rbx, %rbx
    /* rcx is the rip */
    xorq %rdx, %rdx
    xorq %rsi, %rsi
    /* rdi is the arg */
    xorq %rbp, %rbp
    xorq %r8, %r8
    xorq %r9, %r9
    xorq %r10, %r10
    /* r11 is the rflags */
    xorq %r12, %r12
    xorq %r13, %r13
    xorq %r14, %r14
    xorq %r15, %r15

    /**
     * And finally return to usermode
     */
    sysretq
