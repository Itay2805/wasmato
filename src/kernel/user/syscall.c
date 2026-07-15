#include "syscall.h"
#include "arch/smp.h"
#include "irq/ioapic.h"
#include "irq/irq.h"
#include "lib/assert.h"
#include "lib/except.h"
#include "lib/list.h"
#include "lib/log.h"
#include "lib/tsc.h"
#include "mem/direct.h"
#include "mem/phys_map.h"
#include "mem/vmar.h"
#include "uapi/page.h"
#include "uapi/syscall.h"

#include <stdatomic.h>
#include <stdint.h>

#include "limine_requests.h"
#include "runtime.h"
#include "arch/apic.h"
#include "arch/gdt.h"
#include "arch/intrin.h"
#include "arch/regs.h"
#include "lib/ipi.h"
#include "lib/pcpu.h"
#include "lib/rbtree/rbtree.h"
#include "mem/mappings.h"
#include "mem/phys.h"
#include "mem/virt.h"
#include "thread/sched.h"
#include "thread/wait.h"
#include "uapi/wait.h"
#include "user/handle.h"
#include "user/object.h"

/**
 * are we done with early memory
 */
LATE_RO static bool m_early_done = false;

void assert_user_range(const void* addr, size_t size) {
    uintptr_t end;
    ASSERT(addr >= g_user_memory.base);
    ASSERT(!__builtin_add_overflow((uintptr_t)addr, size, &end));
    ASSERT((void*)end <= vmar_end(&g_user_memory));
}

static void copy_string_from_user(char* dst, const char* src, size_t max_size) {
    // ensure the src is within user memory
    ASSERT((void*)src >= g_user_memory.base);
    ASSERT((void*)src < vmar_end(&g_user_memory));

    // modify the max size to stay within the user memory
    uintptr_t end;
    ASSERT(!__builtin_add_overflow((uintptr_t)src, max_size, &end));
    if (end > (uintptr_t)vmar_end(&g_user_memory)) {
        max_size -= end - (uintptr_t)vmar_end(&g_user_memory);
    }

    // now copy over everything from the user
    user_access_enable();
    for (size_t i = 0; i < max_size - 1; i++, dst++, src++) {
        char value = *(char*)src;
        if (value == '\0') {
            break;
        }
        *(char*)dst = value;
    }
    user_access_disable();

    // null terminate it regardless
    *(char*)dst = '\0';
}

static void copy_to_user(void* dst, const void* src, size_t size) {
    assert_user_range(dst, size);

    user_access_enable();
    memcpy((void*)dst, src, size);
    user_access_disable();
}

//----------------------------------------------------------------------------------------------------------------------
// Debug
//----------------------------------------------------------------------------------------------------------------------

static void handle_sys_debug_print(const char* message, size_t message_len) {
    assert_user_range(message, message_len);
    user_access_enable();
    debug_print_raw(message, message_len);
    user_access_disable();
}

//----------------------------------------------------------------------------------------------------------------------
// VMAR management
//----------------------------------------------------------------------------------------------------------------------

static void* handle_sys_mem_reserve(size_t total_page_count, size_t mappable_page_count, const char* name) {
    vmar_lock();

    ASSERT(total_page_count >= mappable_page_count);

    // reserve the top level region
    vmar_t* mapping = vmar_reserve(&g_user_memory, total_page_count, nullptr);
    if (mapping == nullptr) {
        vmar_unlock();
        return nullptr;
    }

    // mark as a mem region, copy the name to it
    mapping->subtype = VMAR_SUBTYPE_MEM;
    copy_string_from_user(mapping->name, name, sizeof(mapping->name));

    // we sometimes want the mappable range to be smaller 
    // than the total reserved range, handle that in here
    vmar_t* bump_parent = mapping;
    if (total_page_count != mappable_page_count) {
        vmar_t* mappable = vmar_reserve(mapping, mappable_page_count, mapping->base);
        if (mappable == nullptr) {
            vmar_free(mapping);
            vmar_unlock();
            return nullptr;
        }

        mappable->subtype = VMAR_SUBTYPE_MAPPABLE;
        vmar_set_name(mappable, "mappable");

        bump_parent = mappable;
    }

    // allocate the bump as a zero sized allocation,
    // we will grow it as required
    vmar_t* bump = vmar_allocate(bump_parent, 0, bump_parent->base);
    if (bump == nullptr) {
        vmar_free(mapping);
        vmar_unlock();
        return nullptr;
    }

    // allocate the bump region inside of the reserved range
    bump->subtype = VMAR_SUBTYPE_BUMP;
    vmar_set_name(bump, "bump");

    void* base = mapping->base;

    vmar_unlock();

    return base;
}

static void* handle_sys_mem_bump(void* ptr, size_t page_count) {
    vmar_lock();

    // get the main mapping
    vmar_t* mapping = vmar_find(&g_user_memory, ptr);
    ASSERT(mapping != nullptr);
    ASSERT(mapping->base == ptr);
    ASSERT(mapping->type == VMAR_TYPE_REGION);
    ASSERT(mapping->subtype == VMAR_SUBTYPE_MEM);

    // get the bump region
    vmar_t* bump = vmar_find_mapping(mapping, ptr);
    ASSERT(bump != nullptr);
    ASSERT(bump->base == ptr);
    ASSERT(bump->type == VMAR_TYPE_ALLOC);
    ASSERT(bump->subtype == VMAR_SUBTYPE_BUMP);

    // get the total that we want to add and
    // ensure it even fits inside
    size_t total_page_count = 0;
    ASSERT(!__builtin_add_overflow(bump->page_count, page_count, &total_page_count));

    void* result = nullptr;
    if (total_page_count <= mapping->page_count) {
        // calculate the end of the new
        // bump region
        void* bump_end = vmar_end(bump);
        void* end = bump_end + PAGES_TO_SIZE(page_count);

        rb_node_t* next = rb_next(&bump->node);
        if (next != nullptr) {
            // we have a next mapping, check it
            vmar_t* vmar = rb_entry(next, vmar_t, node);
            if (end < vmar->base) {
                // there is enough free space in between
                // the next region and the current region
                bump->page_count += page_count;
                result = bump_end + 1;
            }
        } else {
            // no next mapping, we can map
            bump->page_count += page_count;
            result = bump_end + 1;
        }
    }

    vmar_unlock();

    return result;
}

static void* handle_sys_mem_map_phys(void* ptr, uint64_t phys_base, size_t page_count) {
    ASSERT((phys_base % PAGE_SIZE) == 0);

    // convert the range
    if (IS_ERROR(phys_map_to_user(phys_base, PAGES_TO_SIZE(page_count)))) {
        return nullptr;
    }

    // now we can actually map it 
    vmar_lock();

    // get the main mapping
    vmar_t* mapping = vmar_find(&g_user_memory, ptr);
    ASSERT(mapping != nullptr);
    ASSERT(mapping->base == ptr);
    ASSERT(mapping->type == VMAR_TYPE_REGION);
    ASSERT(mapping->subtype == VMAR_SUBTYPE_MEM);

    // find the mappable range, if needed
    vmar_t* mappable = vmar_find(mapping, ptr);
    if (mappable != nullptr && mapping->type == VMAR_TYPE_REGION) {
        ASSERT(mappable->base == ptr);
        ASSERT(mappable->subtype == VMAR_SUBTYPE_MAPPABLE);
    } else {
        // no mappable region, just the main one
        mappable = mapping;
    }

    vmar_t* phys_mapping = vmar_map_phys(mappable, phys_base, page_count, nullptr);
    if (phys_mapping == nullptr) {
        vmar_unlock();
        return nullptr;
    }

    // and finally return it
    ptr = phys_mapping->base;

    vmar_unlock();

    return ptr;
}

static void handle_sys_mem_unmap_phys(void* ptr, size_t page_count) {
    vmar_lock();

    vmar_t* mapping = vmar_find_mapping(&g_user_memory, ptr);
    ASSERT(mapping != nullptr);
    ASSERT(mapping->type == VMAR_TYPE_PHYS);
    ASSERT(mapping->base == ptr);
    ASSERT(mapping->page_count == page_count);
    vmar_free(mapping);

    vmar_unlock();
}

static void handle_sys_mem_free(void* ptr) {
    vmar_lock();

    vmar_t* mapping = vmar_find(&g_user_memory, ptr);
    ASSERT(mapping != nullptr);
    ASSERT(mapping->type == VMAR_TYPE_REGION);
    ASSERT(mapping->subtype == VMAR_SUBTYPE_MEM);
    ASSERT(mapping->base == ptr);
    vmar_free(mapping);

    vmar_unlock();
}

//----------------------------------------------------------------------------------------------------------------------
// Heap management
//----------------------------------------------------------------------------------------------------------------------

static void* handle_sys_heap_alloc(size_t page_count) {
    vmar_lock();

    // allocate a new region with the given page count
    vmar_t* region = vmar_allocate(&g_user_memory, page_count, nullptr);
    if (region == nullptr) {
        // out of memory
        vmar_unlock();
        return nullptr;
    }

    // mark as a heap page, so we can verify it later
    region->subtype = VMAR_SUBTYPE_HEAP;

    // give it a name
    vmar_set_name(region, "heap");

    // we need to read the base before we return it, so 
    // if someone races and tries to unmap it, we will
    // not read an invalid region pointer
    void* base = region->base;

    vmar_unlock();

    return base;
}

static void handle_sys_heap_free(void* base) {
    vmar_lock();

    // find the heap mapping and assert that it is 
    // actually a heap mapping
    vmar_t* mapping = vmar_find_mapping(&g_user_memory, base);
    ASSERT(mapping != nullptr);
    ASSERT(mapping->parent == &g_user_memory);
    ASSERT(mapping->type == VMAR_TYPE_ALLOC);
    ASSERT(mapping->subtype == VMAR_SUBTYPE_HEAP);
    ASSERT(mapping->alloc.protection == MAPPING_PROTECTION_RW);
    
    // free the mapping
    vmar_free(mapping);
    
    vmar_unlock();
}

//----------------------------------------------------------------------------------------------------------------------
// jit management
//----------------------------------------------------------------------------------------------------------------------

static void* handle_sys_jit_alloc(size_t rx_page_count, size_t ro_page_count) {
    vmar_lock();

    // ensure the two don't overflow
    size_t total_pages = 0;
    ASSERT(!__builtin_add_overflow(rx_page_count, ro_page_count, &total_pages));

    // reserve a range for everything, we
    // want it close together
    vmar_t* jit_vmar = vmar_reserve(&g_user_code_region, total_pages, nullptr);
    if (jit_vmar == nullptr) {
        vmar_unlock();
        return nullptr;
    }

    vmar_set_name(jit_vmar, "jit");
    jit_vmar->subtype = VMAR_SUBTYPE_JIT;

    void* end = jit_vmar->base;

    // setup rx pages if any
    if (rx_page_count != 0) {
        vmar_t* rx_vmar = vmar_allocate(jit_vmar, rx_page_count, end);
        if (rx_vmar == nullptr) {
            vmar_free(jit_vmar);
            vmar_unlock();
            return nullptr;
        }

        rx_vmar->subtype = VMAR_SUBTYPE_JIT_RX;
        vmar_set_name(rx_vmar, "text");
        end = vmar_end(rx_vmar) + 1;
    }

    // setup ro pages if any
    if (ro_page_count != 0) {
        vmar_t* ro_vmar = vmar_allocate(jit_vmar, ro_page_count, end);
        if (ro_vmar == nullptr) {
            vmar_free(jit_vmar);
            vmar_unlock();
            return nullptr;
        }

        ro_vmar->subtype = VMAR_SUBTYPE_JIT_RO;
        vmar_set_name(ro_vmar, "rodata");
        end = vmar_end(ro_vmar) + 1;
    }

    // don't allow to modify the top level vmar
    jit_vmar->locked = true;

    // output it
    void* ptr = jit_vmar->base;

    vmar_unlock();

    return ptr;
}

static void handle_sys_jit_lock_protection(void* ptr) {
    vmar_lock();

    // get the first region
    vmar_t* jit_region = vmar_find(&g_user_code_region, ptr);
    ASSERT(jit_region != nullptr);
    ASSERT(jit_region->type == VMAR_TYPE_REGION);
    ASSERT(jit_region->subtype == VMAR_SUBTYPE_JIT);

    // set the RX region
    struct rb_node* first = rb_first(&jit_region->region.root);
    ASSERT(first != nullptr);
    vmar_t* region = rb_entry(first, vmar_t, node);
    ASSERT(region->subtype == VMAR_SUBTYPE_JIT_RX || region->subtype == VMAR_SUBTYPE_JIT_RO);
    vmar_protect(region, region->subtype == VMAR_SUBTYPE_JIT_RX ? MAPPING_PROTECTION_RX : MAPPING_PROTECTION_RO);

    // set the RO region
    struct rb_node* next = rb_next(first);
    if (next != nullptr) {
        ASSERT(next != nullptr);
        vmar_t* ro_region = rb_entry(next, vmar_t, node);
        ASSERT(region->subtype == VMAR_SUBTYPE_JIT_RX);
        ASSERT(ro_region->subtype == VMAR_SUBTYPE_JIT_RO);
        vmar_protect(ro_region, MAPPING_PROTECTION_RO);
    }

    vmar_unlock();
}

static void handle_sys_jit_free(void* ptr) {
    vmar_lock();
    vmar_t* region = vmar_find(&g_user_code_region, ptr);
    ASSERT(region != nullptr);
    ASSERT(region->base == ptr);
    ASSERT(region->type == VMAR_TYPE_REGION);
    ASSERT(region->subtype == VMAR_SUBTYPE_JIT);
    vmar_free(region);
    vmar_unlock();
}

//----------------------------------------------------------------------------------------------------------------------
// Thread handling
//----------------------------------------------------------------------------------------------------------------------

static bool handle_sys_thread_create(void* arg, const char* name) {
    char kname[128];
    copy_string_from_user(kname, name, sizeof(kname));

    // create the new thread
    thread_t* thread = thread_create(
        runtime_thread_entry_thunk,
        arg,
        THREAD_FLAG_USER,
        "%s",
        kname
    );
    if (thread == nullptr) {
        return false;
    }

    // start the thread
    thread_start(thread);

    return true;
}

static void handle_sys_thread_exit(void) {
    // TODO: mark that the thread has 
    //       exited to the thread ripper
    thread_exit();
}

static void handle_sys_thread_yield(void) {
    bool irq_state = irq_save();
    scheduler_schedule();
    irq_restore(irq_state);
}

//----------------------------------------------------------------------------------------------------------------------
// Futex primitives
//----------------------------------------------------------------------------------------------------------------------

static bool handle_sys_atomic_wait(wait_entry_t* wait_entries, size_t count, uint64_t deadline) {
    return atomic_wait(wait_entries, count, deadline);
}

static size_t handle_sys_atomic_notify(void* key, uint64_t mask, size_t count) {
    return atomic_notify(key, mask, count);
}

//----------------------------------------------------------------------------------------------------------------------
// Handle syscalls
//----------------------------------------------------------------------------------------------------------------------

static void handle_sys_handle_close(uint64_t handle) {
    handle_close(handle);
}

//----------------------------------------------------------------------------------------------------------------------
// IRQ handling
//----------------------------------------------------------------------------------------------------------------------

static uint64_t handle_sys_irq_create_ioapic(wake_params_t* user_wake_params, uint32_t irq_num, uint32_t cpu_id) {
    // get a copy of the wake params and validate it 
    assert_user_range(user_wake_params, sizeof(*user_wake_params));
    user_access_enable();
    wake_params_t wake_params = *user_wake_params;
    user_access_disable();
    assert_user_range(wake_params.key, wake_params.key_size == WAIT_KEY_UINT32 ? 4 : 8);

    // make sure the id is in a valid range
    ASSERT(cpu_id < g_cpu_count);

    // create the interrupt object
    irq_t* irq = irq_create(cpu_id);
    if (irq == nullptr) {
        return INVALID_HANDLE;
    }

    // copy over the wake params
    irq->wait_key = wake_params.key;
    irq->wait_key_size = wake_params.key_size;
    irq->wait_mask = wake_params.mask;

    // register the ioapic interrupt
    ioapic_register_isa(irq, irq_num);

    // create the handle for it
    uint64_t handle = handle_register(irq);
    if (handle == INVALID_HANDLE) {
        kernel_object_put(&irq->object);
        return INVALID_HANDLE;
    }

    return handle;
}

static void handle_sys_irq_unmask(uint64_t handle) {
    kernel_object_t* object = handle_lookup(handle);
    ASSERT(object->type == KERNEL_OBJECT_TYPE_IRQ);
    irq_t* irq = containerof(object, irq_t, object);
    irq_unmask(irq);
    kernel_object_put(object);
}

//----------------------------------------------------------------------------------------------------------------------
// Early syscalls for configuring stuff from the runtime
//----------------------------------------------------------------------------------------------------------------------

INIT_CODE static size_t handle_sys_early_get_initrd_size(void) {
    struct limine_file* file = g_limine_module_request.response->modules[0];
    return file->size;
}

INIT_CODE static void handle_sys_early_get_initrd(void* addr) {
    struct limine_file* file = g_limine_module_request.response->modules[0];
    copy_to_user(addr, file->address, file->size);
}

INIT_CODE static uint64_t handle_sys_early_get_tsc_freq(void) {
	return g_tsc_freq_hz;
}

INIT_CODE static uint64_t handle_sys_early_get_rsdp(void) {
    if (g_limine_rsdp_request.response != nullptr) {
        return direct_to_phys(g_limine_rsdp_request.response->address);
    } else {
        return 0;
    }
}

INIT_CODE static void handle_sys_early_done(void) {
    // ensure all cores have the scheduler
    // enabled properly before we start
    ipi_broadcast(IPI_SYNC_EARLY_DONE);

    // ensure we are not done yet
    ASSERT(!m_early_done);
    m_early_done = true;

    // we don't need the bootloader memory anymore
    ASSERT(!IS_ERROR(reclaim_bootloader_memory()));

    // reprotect data that should be read-only
    protect_ro_data();
}

OMIT_ENDBR uint64_t syscall_handler(syscall_t syscall, uint64_t arg1, uint64_t arg2, uint64_t rip, uint64_t arg3, uint64_t arg4) {
    switch (syscall) {
        case SYSCALL_DEBUG_PRINT: handle_sys_debug_print((void*)arg1, arg2); break;
        case SYSCALL_HEAP_ALLOC: return (uintptr_t)handle_sys_heap_alloc(arg1); break;
        case SYSCALL_HEAP_FREE: handle_sys_heap_free((void*)arg1); break;
        case SYSCALL_MEM_RESERVE: return (uintptr_t)handle_sys_mem_reserve(arg1, arg2, (void*)arg3); break;
        case SYSCALL_MEM_BUMP: return (uintptr_t)handle_sys_mem_bump((void*)arg1, arg2); break;
        case SYSCALL_MEM_MAP_PHYS: return (uintptr_t)handle_sys_mem_map_phys((void*)arg1, arg2, arg3); break;
        case SYSCALL_MEM_UNMAP_PHYS: handle_sys_mem_unmap_phys((void*)arg1, arg2); break;
        case SYSCALL_MEM_FREE: handle_sys_mem_free((void*)arg1); break;
        case SYSCALL_JIT_ALLOC: return (uintptr_t)handle_sys_jit_alloc(arg1, arg2); break;
        case SYSCALL_JIT_LOCK_PROTECTION: handle_sys_jit_lock_protection((void*)arg1); break;
        case SYSCALL_JIT_FREE: handle_sys_jit_free((void*)arg1); break;
        case SYSCALL_THREAD_CREATE: return handle_sys_thread_create((void*)arg1, (void*)arg2); break;
        case SYSCALL_THREAD_EXIT: handle_sys_thread_exit(); break;
        case SYSCALL_THREAD_YIELD: handle_sys_thread_yield(); break;
        case SYSCALL_ATOMIC_WAIT: return handle_sys_atomic_wait((void*)arg1, arg2, arg3); break;
        case SYSCALL_ATOMIC_NOTIFY: return handle_sys_atomic_notify((void*)arg1, arg2, arg3); break;
        case SYSCALL_HANDLE_CLOSE: handle_sys_handle_close(arg1); break;
        case SYSCALL_IRQ_CREATE_IOAPIC: return handle_sys_irq_create_ioapic((void*)arg1, arg2, arg3); break;
        case SYSCALL_IRQ_UNMASK: handle_sys_irq_unmask(arg1); break;

        case SYSCALL_EARLY_GET_INITRD_SIZE: {
            ASSERT(!m_early_done);
            return handle_sys_early_get_initrd_size();
        } break;

        case SYSCALL_EARLY_GET_INITRD: {
            ASSERT(!m_early_done);
            handle_sys_early_get_initrd((void*)arg1);
        } break;

        case SYSCALL_EARLY_GET_TSC_FREQ: {
            ASSERT(!m_early_done);
            return handle_sys_early_get_tsc_freq();
        } break;

        case SYSCALL_EARLY_GET_RSDP: {
            ASSERT(!m_early_done);
            return handle_sys_early_get_rsdp();
        } break;

        case SYSCALL_EARLY_DONE: {
            ASSERT(!m_early_done);
            handle_sys_early_done();
            reclaim_init_mem();
        } break;

        default:
            ASSERT(false, "syscall: Unknown syscall: %d", syscall);
    }

    return 0;
}

// this is called directly by the stub and no-one else

void syscall_entry(void);

INIT_CODE void init_syscall() {
    // setup the main descriptors
    __wrmsr(MSR_IA32_STAR, (GDT_KERNEL_CODE << 32) | ((GDT_USER_CODE - 16) << 48));

    // setup the entry points
    __wrmsr(MSR_IA32_LSTAR, (uintptr_t)syscall_entry);
    __wrmsr(MSR_IA32_CSTAR, 0);

    // mask basically all the flags we can
    rflags_t flags = {
        .CF = 1,
        .PF = 1,
        .AF = 1,
        .ZF = 1,
        .SF = 1,
        .TF = 1,
        .IF = 1,
        .DF = 1,
        .OF = 1,
        .IOPL = 0b11,
        .NT = 1,
        .RF = 1,
        .AC = 1,
        .ID = 1,
    };
    __wrmsr(MSR_IA32_FMASK, flags.packed);
}
