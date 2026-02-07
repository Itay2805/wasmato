#pragma once

typedef enum syscall {
    /**
     * arg1 - string to print
     * arg2 - the length of the string to print
     */
    SYSCALL_DEBUG_PRINT,

    SYSCALL_
} syscall_t;

#define syscall0(num) \
    ({ \
	    long _ret; \
	    register long _num  __asm__ ("rax") = (num); \
	    __asm__ volatile ( \
		    "syscall\n" \
		    : "=a"(_ret) \
		    : "0"(_num) \
		    : "rcx", "r11", "memory", "cc" \
	    ); \
	    _ret; \
    })

#define syscall1(num, arg1) \
    ({ \
	    long _ret; \
	    register long _num  __asm__ ("rax") = (num); \
	    register long _arg1 __asm__ ("rdi") = (long)(arg1); \
	    __asm__ volatile ( \
		    "syscall\n" \
		    : "=a"(_ret) \
		    : "r"(_arg1), \
		      "0"(_num) \
		    : "rcx", "r11", "memory", "cc" \
	    ); \
	    _ret; \
    })

#define syscall2(num, arg1, arg2) \
    ({ \
	    long _ret; \
	    register long _num  __asm__ ("rax") = (num); \
	    register long _arg1 __asm__ ("rdi") = (long)(arg1); \
	    register long _arg2 __asm__ ("rsi") = (long)(arg2); \
	    __asm__ volatile ( \
		    "syscall\n" \
		    : "=a"(_ret) \
		    : "r"(_arg1), "r"(_arg2), \
		      "0"(_num) \
		    : "rcx", "r11", "memory", "cc" \
	    ); \
	    _ret; \
    })

#define syscall3(num, arg1, arg2, arg3) \
    ({ \
	    long _ret; \
	    register long _num  __asm__ ("rax") = (num); \
	    register long _arg1 __asm__ ("rdi") = (long)(arg1); \
	    register long _arg2 __asm__ ("rsi") = (long)(arg2); \
	    register long _arg3 __asm__ ("rdx") = (long)(arg3); \
	    __asm__ volatile ( \
		    "syscall\n" \
		    : "=a"(_ret) \
		    : "r"(_arg1), "r"(_arg2), "r"(_arg3), \
		      "0"(_num) \
		    : "rcx", "r11", "memory", "cc" \
	    ); \
	    _ret; \
    })

#define syscall4(num, arg1, arg2, arg3, arg4) \
    ({ \
	    long _ret; \
	    register long _num  __asm__ ("rax") = (num); \
	    register long _arg1 __asm__ ("rdi") = (long)(arg1); \
	    register long _arg2 __asm__ ("rsi") = (long)(arg2); \
	    register long _arg3 __asm__ ("rdx") = (long)(arg3); \
	    register long _arg4 __asm__ ("r10") = (long)(arg4); \
	    __asm__ volatile ( \
		    "syscall\n" \
		    : "=a"(_ret) \
		    : "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), \
		      "0"(_num) \
		    : "rcx", "r11", "memory", "cc" \
	    ); \
	    _ret; \
    })

#define syscall5(num, arg1, arg2, arg3, arg4, arg5) \
    ({ \
	    long _ret; \
	    register long _num  __asm__ ("rax") = (num); \
	    register long _arg1 __asm__ ("rdi") = (long)(arg1); \
	    register long _arg2 __asm__ ("rsi") = (long)(arg2); \
	    register long _arg3 __asm__ ("rdx") = (long)(arg3); \
	    register long _arg4 __asm__ ("r10") = (long)(arg4); \
	    register long _arg5 __asm__ ("r8")  = (long)(arg5); \
	    __asm__ volatile ( \
		    "syscall\n" \
		    : "=a"(_ret) \
		    : "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5), \
		      "0"(_num) \
		    : "rcx", "r11", "memory", "cc" \
	    ); \
	    _ret; \
    })

#define syscall6(num, arg1, arg2, arg3, arg4, arg5, arg6) \
    ({ \
	    long _ret; \
	    register long _num  __asm__ ("rax") = (num); \
	    register long _arg1 __asm__ ("rdi") = (long)(arg1); \
	    register long _arg2 __asm__ ("rsi") = (long)(arg2); \
	    register long _arg3 __asm__ ("rdx") = (long)(arg3); \
	    register long _arg4 __asm__ ("r10") = (long)(arg4); \
	    register long _arg5 __asm__ ("r8")  = (long)(arg5); \
	    register long _arg6 __asm__ ("r9")  = (long)(arg6); \
	    __asm__ volatile ( \
		    "syscall\n" \
		    : "=a"(_ret) \
		    : "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5), \
		      "r"(_arg6), "0"(_num) \
		    : "rcx", "r11", "memory", "cc" \
	    ); \
	    _ret; \
    })

