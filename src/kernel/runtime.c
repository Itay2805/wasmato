#include "runtime.h"

#include <stdint.h>

#include "arch/intr.h"
#include "arch/intrin.h"
#include "arch/smp.h"
#include "lib/elf64.h"
#include "lib/pcpu.h"
#include "lib/printf.h"
#include "../runtime/lib/string.h"
#include "lib/tsc.h"
#include "uapi/entry.h"
#include "user/user.h"
#include "mem/mappings.h"
#include "mem/phys.h"
#include "user/stack.h"

/**
 * The elf of the usermode runtime
 *
 * TODO: map directly from this, so we can use the VT-d PMRs
 *       to protect it from DMA more easily
 */
INIT_DATA static char m_runtime_elf[] = {
    #embed "runtime"
};

#define ELF_PTR(type, offset) \
    ({ \
        size_t offset__ = offset; \
        size_t top_address__; \
        CHECK(!__builtin_add_overflow(offset__, sizeof(type), &top_address__)); \
        CHECK(ARRAY_LENGTH(m_runtime_elf) >= top_address__); \
        (type*)&m_runtime_elf[offset__]; \
    })

#define ELF_ARR(type, count, offset) \
    ({ \
        size_t size__ = sizeof(type); \
        size_t count__ = count; \
        CHECK(!__builtin_mul_overflow(size__, count__, &size__)); \
        size_t offset__ = offset; \
        size_t top_address__; \
        CHECK(!__builtin_add_overflow(offset__, sizeof(type), &top_address__)); \
        CHECK(ARRAY_LENGTH(m_runtime_elf) >= top_address__); \
        (type*)&m_runtime_elf[offset__]; \
    })

/**
 * the runtime entry point, called on all cores with the first argument
 * being the cpu index
 */
INIT_DATA static void* m_runtime_entry_point = NULL;

/**
 * The TLS data, null if not found
 */
INIT_DATA static Elf64_Phdr* m_runtime_tls_phdr = nullptr;

INIT_DATA static void** m_runtime_early_stacks = nullptr;

INIT_CODE void runtime_free_early_stacks(void) {
    TRACE("runtime: freeing early stacks");
    for (int i = 0; i < g_cpu_count; i++) {
        user_stack_free(m_runtime_early_stacks[i]);
    }
}

INIT_CODE static err_t runtime_elf_verify_header(void) {
    err_t err = NO_ERROR;

    Elf64_Ehdr* ehdr = ELF_PTR(Elf64_Ehdr, 0);

    CHECK(ehdr->e_ident[EI_MAG0] == ELFMAG0);
    CHECK(ehdr->e_ident[EI_MAG1] == ELFMAG1);
    CHECK(ehdr->e_ident[EI_MAG2] == ELFMAG2);
    CHECK(ehdr->e_ident[EI_MAG3] == ELFMAG3);
    CHECK(ehdr->e_ident[EI_CLASS] == ELFCLASS64);
    CHECK(ehdr->e_ident[EI_DATA] == ELFDATA2LSB);
    CHECK(ehdr->e_ident[EI_VERSION] == EV_CURRENT);
    CHECK(ehdr->e_ident[EI_ABIVERSION] == 0);
    CHECK(ehdr->e_machine == EM_X86_64);
    CHECK(ehdr->e_type == ET_EXEC);

    CHECK(ehdr->e_phentsize == sizeof(Elf64_Phdr));

cleanup:
    return err;
}

INIT_CODE static err_t runtime_elf_get_range(uintptr_t* load_address, size_t* top_address) {
    err_t err = NO_ERROR;

    Elf64_Ehdr* ehdr = ELF_PTR(Elf64_Ehdr, 0);
    Elf64_Phdr* phdrs = ELF_ARR(Elf64_Phdr, ehdr->e_phnum, ehdr->e_phoff);

    for (int i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr* phdr = &phdrs[i];
        if (phdr->p_type != PT_LOAD)
            continue;

        // align to page
        size_t end = 0;
        CHECK(!__builtin_add_overflow(phdr->p_vaddr, phdr->p_memsz, &end));
        *load_address = MIN(phdr->p_vaddr, *load_address);
        *top_address = MAX(end, *top_address);
    }

    // align to page
    *load_address = ALIGN_DOWN(*load_address, PAGE_SIZE);
    *top_address = ALIGN_UP(*top_address, PAGE_SIZE);

cleanup:
    return err;
}

INIT_CODE static err_t runtime_elf_map(void) {
    err_t err = NO_ERROR;

    Elf64_Ehdr* ehdr = ELF_PTR(Elf64_Ehdr, 0);
    Elf64_Phdr* phdrs = ELF_ARR(Elf64_Phdr, ehdr->e_phnum, ehdr->e_phoff);

    // now actually map it
    TRACE("runtime: mapping runtime");
    for (int i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr* phdr = &phdrs[i];
        if (phdr->p_type != PT_LOAD)
            continue;

        // choose the name based on permissions, just something that works
        const char* name = nullptr;
        switch (phdr->p_flags) {
            case PF_R: name = "rodata"; break;
            case PF_R | PF_W: name = "data"; break;
            case PF_R | PF_X: name = "text"; break;
            default: CHECK_FAIL();
        }
        TRACE("runtime: \t%p-%p: %s", (void*)phdr->p_vaddr, (void*)phdr->p_vaddr + phdr->p_memsz, name);

        // align to page
        size_t aligned_start = ALIGN_DOWN(phdr->p_vaddr, PAGE_SIZE);
        size_t aligned_end = ALIGN_UP(phdr->p_vaddr + phdr->p_memsz, PAGE_SIZE);
        size_t aligned_size = aligned_end - aligned_start;

        // setup the region
        vmar_t* region = vmar_allocate(&g_runtime_region, SIZE_TO_PAGES(aligned_size), (void*)aligned_start);
        CHECK_ERROR(region != NULL, ERROR_OUT_OF_MEMORY);
        vmar_set_name(region, name);
        region->pinned = true;

        // copy the data, we need to enable accessing user memory while we do that
        asm("stac");
        void* data = ELF_ARR(char, phdr->p_filesz, phdr->p_offset);
        memset((void*)phdr->p_vaddr, 0, SIZE_TO_PAGES(phdr->p_memsz));
        if (phdr->p_filesz != 0) {
            memcpy((void*)phdr->p_vaddr, data, phdr->p_filesz);
        }
        asm("clac");
    }

    // we created all mappings, we can lock the runtime
    g_runtime_region.locked = true;

    // TODO: apply relocations

    // and now apply the real protections
    for (int i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr* phdr = &phdrs[i];
        if (phdr->p_type != PT_LOAD)
            continue;


        // choose the protections
        mapping_protection_t protection;
        switch (phdr->p_flags) {
            case PF_R: protection = MAPPING_PROTECTION_RO; break;
            case PF_R | PF_W: protection = MAPPING_PROTECTION_RW; break;
            case PF_R | PF_X: protection = MAPPING_PROTECTION_RX; break;
            default: CHECK_FAIL();
        }

        // set the correct protections now, this will also lock the region
        vmar_protect_ptr((void*)ALIGN_DOWN(phdr->p_vaddr, PAGE_SIZE), protection);
    }

cleanup:
    return err;
}

INIT_CODE static err_t runtime_elf_parse_tls(void) {
    err_t err = NO_ERROR;

    Elf64_Ehdr* ehdr = ELF_PTR(Elf64_Ehdr, 0);
    Elf64_Phdr* phdrs = ELF_ARR(Elf64_Phdr, ehdr->e_phnum, ehdr->e_phoff);

    for (int i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr* phdr = &phdrs[i];
        if (phdr->p_type != PT_TLS)
            continue;

        CHECK(m_runtime_tls_phdr == nullptr);
        m_runtime_tls_phdr = phdr;
    }

    if (m_runtime_tls_phdr != nullptr) {
        // TODO: support for init data
        CHECK(m_runtime_tls_phdr->p_filesz == 0);
    }

cleanup:
    return err;
}

INIT_CODE err_t load_runtime(void) {
    err_t err = NO_ERROR;

    // allocate space for all the stacks
    m_runtime_early_stacks = phys_alloc(SIZE_TO_PAGES(sizeof(void*) * g_cpu_count));
    CHECK(m_runtime_early_stacks != nullptr);

    vmar_lock();

    // reserve the entire user code region
    CHECK(vmar_reserve_static(&g_user_memory, &g_user_code_region));

    // validate the elf header
    RETHROW(runtime_elf_verify_header());

    // get the load address and top address of the elf, so
    // we can reserve the top region
    uintptr_t elf_load_address = -1;
    size_t elf_top_address = 0;
    RETHROW(runtime_elf_get_range(&elf_load_address, &elf_top_address));

    // ensure we have at least 4kb before the load address
    CHECK(elf_load_address >= BASE_4KB);

    // setup the runtime region, this should have the entire elf inside of it
    g_runtime_region.base = (void*)elf_load_address;
    g_runtime_region.page_count = SIZE_TO_PAGES(elf_top_address - elf_load_address);
    CHECK(vmar_reserve_static(&g_user_code_region, &g_runtime_region));

    // actually map the elf
    RETHROW(runtime_elf_map());

    // parse the tls so we can map it
    RETHROW(runtime_elf_parse_tls());

    // save the entry point so we can jump to it
    Elf64_Ehdr* ehdr = ELF_PTR(Elf64_Ehdr, 0);
    m_runtime_entry_point = (void*)ehdr->e_entry;

cleanup:
    vmar_unlock();

    return err;
}

INIT_CODE void runtime_start(void) {
    // set the name of the stack
    char name[32];
    snprintf_(name, sizeof(name), "early-stack-%d", get_cpu_id());

    // allocate a stack, it will be used as the scheduler stack for the runtime
    stack_alloc_t stack_alloc = {};
    ASSERT(!IS_ERROR(user_stack_alloc(&stack_alloc, name, SIZE_4KB)));

    // remember the stack for freeing later
    m_runtime_early_stacks[get_cpu_id()] = stack_alloc.stack;

    // setup the runtime params
    void* user_stack = stack_alloc.stack;
    user_stack -= sizeof(runtime_params_t);
    user_stack = ALIGN_DOWN(user_stack, 16);

    // we need to write to the usermode stack, lock for write
    asm("stac");

    // the cpu metadata
    runtime_params_t* params = user_stack;
    params->cpu_id = get_cpu_id();
    params->cpu_count = g_cpu_count;
    params->tsc_freq = g_tsc_freq_hz;

    // tls info
    // TODO: somehow pass the init data
    params->tls_size = m_runtime_tls_phdr != nullptr ? m_runtime_tls_phdr->p_memsz : 0;

    // setup the vector info
    params->timer_vector = INTR_VECTOR_TIMER;
    params->first_vector = INTR_VECTOR_TIMER + 1;
    params->last_vector = INTR_VECTOR_SPURIOUS - 16;

    // lock usermode again
    asm("clac");

    // for dummy region
    user_stack -= 16;

    // if we have a shadow stack set it now
    if (stack_alloc.shadow_stack) {
        __wrmsr(MSR_IA32_PL3_SSP, (uint64_t)stack_alloc.shadow_stack);
    }

    // and jump to it
    usermode_jump(params, m_runtime_entry_point, user_stack);
}
