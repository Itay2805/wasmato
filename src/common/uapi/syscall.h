#pragma once

#include <stddef.h>
#include <stdint.h>

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
#include "lib/string.h"

typedef enum syscall {
    /**
     * Print to the debug console, used for early debugging
     *  arg1 - string to print
     *  arg2 - the length of the string to print
     */
    SYSCALL_DEBUG_PRINT,

    /**
     * Allocate memory in page granularity, can be anywhere
     * in the usermode virtual address space, will be locked
	 * to read-write access
     *  arg1 - page count
	 *	ret - pointer to allocated region (rw), NULL if out of memory
     */
    SYSCALL_HEAP_ALLOC,

	/**
	 * Free an existing heap allocation, must give the exact
	 * base address of the allocation
	 *	arg1 - pointer to allocated region
	 */
	SYSCALL_HEAP_FREE,

	/**
	 * Allocate pages meant for jit allocation
	 *	arg1 - page count
	 *	arg2 - name
	 *	ret - pointer to allocated region (rw), NULL if out of memory
	 */
	SYSCALL_JIT_ALLOC,

	/**
	 * Lock pages into a specific protection, once locked
	 * the protection can't be changed again
	 *	arg1 - pointer to allocated region
	 *	arg2 - allow write
	 * 	arg3 - allow execute
	 */
	SYSCALL_JIT_LOCK_PROTECTION,

	/**
	 * Free jit pages
	 *	arg1 - pointer to allocated region
	 */
	SYSCALL_JIT_FREE,

	/**
	 * Reserve a memory region
	 *	arg1 - page count
	 *	arg2 - name
	 *	ret - pointer to reserved region, NULL if out of memory
	 */
	SYSCALL_MEM_RESERVE,

	/**
	 * Map physical memory into a reserved region
	 *	arg1 - pointer to reserved region
	 *	arg2 - physical address
	 * 	arg3 - page count
	 *	ret - pointer to mapped region, NULL if out of memory
	 */
	SYSCALL_MEM_MAP_PHYS,

	/**
	 * Bump the memory region inside of a reserved region
	 * into the given address
	 *	arg1 - pointer to new bump address
	 *	ret - true if success, false if out of memory
	 */
	SYSCALL_MEM_BUMP,

	/**
	 * Release reserved region
	 * 	arg1 - pointer to reserved region
	 */
	SYSCALL_MEM_RELEASE,
} syscall_t;

static inline void sys_debug_print(const char* message, size_t message_len) {
    syscall2(SYSCALL_DEBUG_PRINT, message, message_len);
}

static inline void* sys_heap_alloc(size_t page_count) {
    return (void*)syscall1(SYSCALL_HEAP_ALLOC, page_count);
}

static inline void sys_heap_free(void* base) {
    (void)syscall1(SYSCALL_HEAP_FREE, base);
}
