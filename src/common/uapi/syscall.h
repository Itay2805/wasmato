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

typedef enum syscall {
    SYSCALL_DEBUG_PRINT,

    SYSCALL_HEAP_ALLOC,
	SYSCALL_HEAP_FREE,

	SYSCALL_JIT_ALLOC,
	SYSCALL_JIT_LOCK_PROTECTION,
	SYSCALL_JIT_FREE,

	SYSCALL_MEM_RESERVE,
	SYSCALL_MEM_MAP_PHYS,
	SYSCALL_MEM_BUMP,
	SYSCALL_MEM_RELEASE,

	SYSCALL_STACK_ALLOC,
	SYSCALL_STACK_FREE,

	SYSCALL_TIMER_SET_DEADLINE,
	SYSCALL_TIMER_CLEAR,

	SYSCALL_INTERRUPT_ACK,

	SYSCALL_MONITOR_WAIT,

	SYSCALL_EARLY_INTERRUPT_SET_HANDLER,
	SYSCALL_EARLY_SET_THREAD_ENTRY_THUNK,
	SYSCALL_EARLY_GET_INITRD_SIZE,
	SYSCALL_EARLY_GET_INITRD,
    SYSCALL_EARLY_DONE,
} syscall_t;

//----------------------------------------------------------------------------------------------------------------------
// Debug
//----------------------------------------------------------------------------------------------------------------------

static inline void sys_debug_print(const char* message, size_t message_len) {
    syscall2(SYSCALL_DEBUG_PRINT, message, message_len);
}

//----------------------------------------------------------------------------------------------------------------------
// Heap management
//----------------------------------------------------------------------------------------------------------------------

static inline void* sys_heap_alloc(size_t page_count) {
    return (void*)syscall1(SYSCALL_HEAP_ALLOC, page_count);
}

static inline void sys_heap_free(void* base) {
    (void)syscall1(SYSCALL_HEAP_FREE, base);
}

//----------------------------------------------------------------------------------------------------------------------
// jit management
//----------------------------------------------------------------------------------------------------------------------

static inline void* sys_jit_alloc(size_t rx_page_count, size_t ro_page_count) {
	return (void*)syscall2(SYSCALL_JIT_ALLOC, rx_page_count, ro_page_count);
}

static inline void sys_jit_lock_protection(void* ptr) {
	(void)syscall1(SYSCALL_JIT_LOCK_PROTECTION, ptr);
}

static inline void sys_jit_free(void* ptr) {
	(void)syscall1(SYSCALL_JIT_FREE, ptr);
}

//----------------------------------------------------------------------------------------------------------------------
// Stack management
//----------------------------------------------------------------------------------------------------------------------

typedef struct sys_stack_alloc {
	void* stack;
	void* shadow_stack;
} sys_stack_alloc_t;

static inline sys_stack_alloc_t sys_stack_alloc(size_t stack_size, const char* name) {
	// this syscall returns two values, so we need to do this
	// with a bit of inline asm
	sys_stack_alloc_t alloc = {};
	__asm__ volatile (
		 "syscall\n"
		 : "=a"(alloc.stack)
		 , "=d"(alloc.shadow_stack)
		 : "a"(SYSCALL_STACK_ALLOC),
		   "D"(stack_size),
		   "S"((uintptr_t)name)
		 : "rcx", "r11", "memory", "cc"
	);
	return alloc;
}

static inline void sys_stack_free(void* base) {
	(void)syscall1(SYSCALL_HEAP_FREE, base);
}

//----------------------------------------------------------------------------------------------------------------------
// Timer management
//----------------------------------------------------------------------------------------------------------------------

static inline void sys_timer_set_deadline(uintptr_t tsc_deadline) {
	(void)syscall1(SYSCALL_TIMER_SET_DEADLINE, tsc_deadline);
}

static inline void sys_timer_clear(void) {
	(void)syscall0(SYSCALL_TIMER_CLEAR);
}

//----------------------------------------------------------------------------------------------------------------------
// Interrupt management
//----------------------------------------------------------------------------------------------------------------------

static inline void sys_interrupt_ack(void) {
	(void)syscall0(SYSCALL_INTERRUPT_ACK);
}

static inline void sys_monitor_wait(_Atomic(uint32_t)* addr, uint32_t expected) {
	(void)syscall2(SYSCALL_MONITOR_WAIT, addr, expected);
}

//----------------------------------------------------------------------------------------------------------------------
// Early syscalls for configuring stuff from the runtime
//----------------------------------------------------------------------------------------------------------------------

typedef struct interrupt_frame {
	uint64_t rip;
	uint64_t cs;
	uint64_t rflags;
	uint64_t rsp;
	uint64_t ss;
} interrupt_frame_t;

__attribute__((interrupt))
typedef void (*interrupt_handler_t)(interrupt_frame_t* frame);

static inline void sys_early_interrupt_set_handler(uint8_t vector, interrupt_handler_t handler) {
	(void)syscall2(SYSCALL_EARLY_INTERRUPT_SET_HANDLER, vector, handler);
}

static inline void sys_early_set_thread_entry_thunk(void* addr) {
	(void)syscall1(SYSCALL_EARLY_SET_THREAD_ENTRY_THUNK, addr);
}

static inline size_t sys_early_get_initrd_size(void) {
	return syscall0(SYSCALL_EARLY_GET_INITRD_SIZE);
}

static inline void sys_early_get_initrd(void* addr) {
	(void)syscall1(SYSCALL_EARLY_GET_INITRD, addr);
}

static inline void sys_early_done(void) {
	(void)syscall0(SYSCALL_EARLY_DONE);
}
