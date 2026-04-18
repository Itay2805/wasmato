#include "syscall.h"
#include "uapi/syscall.h"

#define syscall0(num) \
    ({ \
	    long _ret; \
	    register long _num  __asm__ ("rdi") = (num); \
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
	    register long _num  __asm__ ("rdi") = (num); \
	    register long _arg1 __asm__ ("rsi") = (long)(arg1); \
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
	    register long _num  __asm__ ("rdi") = (num); \
	    register long _arg1 __asm__ ("rsi") = (long)(arg1); \
	    register long _arg2 __asm__ ("rdx") = (long)(arg2); \
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
	    register long _num  __asm__ ("rdi") = (num); \
	    register long _arg1 __asm__ ("rsi") = (long)(arg1); \
	    register long _arg2 __asm__ ("rdx") = (long)(arg2); \
	    register long _arg3 __asm__ ("r8") = (long)(arg3); \
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
	    register long _num  __asm__ ("rdi") = (num); \
	    register long _arg1 __asm__ ("rsi") = (long)(arg1); \
	    register long _arg2 __asm__ ("rdx") = (long)(arg2); \
	    register long _arg3 __asm__ ("r8") = (long)(arg3); \
	    register long _arg4 __asm__ ("r9") = (long)(arg4); \
	    __asm__ volatile ( \
		    "syscall\n" \
		    : "=a"(_ret) \
		    : "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), \
		      "0"(_num) \
		    : "rcx", "r11", "memory", "cc" \
	    ); \
	    _ret; \
    })


//----------------------------------------------------------------------------------------------------------------------
// Debug
//----------------------------------------------------------------------------------------------------------------------

void sys_debug_print(const char* message, size_t message_len) {
    syscall2(SYSCALL_DEBUG_PRINT, message, message_len);
}

//----------------------------------------------------------------------------------------------------------------------
// Heap management
//----------------------------------------------------------------------------------------------------------------------

void* sys_heap_alloc(size_t page_count) {
    return (void*)syscall1(SYSCALL_HEAP_ALLOC, page_count);
}

void sys_heap_free(void* base) {
    (void)syscall1(SYSCALL_HEAP_FREE, base);
}

//----------------------------------------------------------------------------------------------------------------------
// jit management
//----------------------------------------------------------------------------------------------------------------------

void* sys_jit_alloc(size_t rx_page_count, size_t ro_page_count) {
	return (void*)syscall2(SYSCALL_JIT_ALLOC, rx_page_count, ro_page_count);
}

void sys_jit_lock_protection(void* ptr) {
	(void)syscall1(SYSCALL_JIT_LOCK_PROTECTION, ptr);
}

void sys_jit_free(void* ptr) {
	(void)syscall1(SYSCALL_JIT_FREE, ptr);
}

//----------------------------------------------------------------------------------------------------------------------
// Thread handling
//----------------------------------------------------------------------------------------------------------------------

typedef void (*sys_thread_entry_t)(void* arg);

bool sys_thread_create(void* arg, const char* name) {
	return (bool)syscall2(SYSCALL_THREAD_CREATE, arg, name);
}

void sys_thread_sleep(size_t ms) {
	(void)syscall1(SYSCALL_THREAD_SLEEP, ms);
}

void sys_thread_exit(void) {
	(void)syscall0(SYSCALL_THREAD_EXIT);
}

//----------------------------------------------------------------------------------------------------------------------
// Futex primitives
//----------------------------------------------------------------------------------------------------------------------

void sys_atomic_wait32(void* key, uint64_t old, uint64_t deadline) {
	(void)syscall3(SYSCALL_ATOMIC_WAIT32, key, old, deadline);
}

void sys_atomic_wait64(void* key, uint64_t old, uint64_t deadline) {
	(void)syscall3(SYSCALL_ATOMIC_WAIT64, key, old, deadline);
}

size_t sys_atomic_notify(void* key, size_t count) {
	return syscall2(SYSCALL_ATOMIC_NOTIFY, key, count);
}

//----------------------------------------------------------------------------------------------------------------------
// Early syscalls for configuring stuff from the runtime
//----------------------------------------------------------------------------------------------------------------------

size_t sys_early_get_initrd_size(void) {
	return syscall0(SYSCALL_EARLY_GET_INITRD_SIZE);
}

void sys_early_get_initrd(void* addr) {
	(void)syscall1(SYSCALL_EARLY_GET_INITRD, addr);
}

void sys_early_done(void) {
	(void)syscall0(SYSCALL_EARLY_DONE);
}
