#include "runtime.h"

#include <stdint.h>

#include "lib/elf64.h"
#include "lib/elf_common.h"
#include "lib/string.h"
#include "mem/mappings.h"
#include "thread/scheduler.h"
#include "thread/thread.h"

/**
 * The elf of the usermode runtime
 */
static char m_runtime_elf[] = {
    #embed "build/runtime"
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
err_t load_and_start_runtime(void) {
    err_t err = NO_ERROR;

    // validate the elf header
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

    // get the load address and top address of the elf, so
    // we can reserve the top region
    uintptr_t elf_load_address = -1;
    size_t elf_top_address = 0;
    CHECK(ehdr->e_phentsize == sizeof(Elf64_Phdr));
    Elf64_Phdr* phdrs = ELF_ARR(Elf64_Phdr, ehdr->e_phnum, ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr* phdr = &phdrs[i];
        if (phdr->p_type != PT_LOAD)
            continue;

        // align to page
        uintptr_t vaddr = ALIGN_DOWN(phdr->p_vaddr, PAGE_SIZE);

        // align to page
        size_t top_address;
        CHECK(!__builtin_add_overflow(vaddr, phdr->p_memsz, &top_address));
        top_address = ALIGN_UP(top_address, PAGE_SIZE);

        elf_load_address = MIN(vaddr, elf_load_address);
        elf_top_address = MAX(top_address, elf_top_address);
    }

    // ensure we have at least 4kb before the load address
    CHECK(elf_load_address >= BASE_4KB);

    // setup the runtime region, this should have the entire elf inside of it
    g_runtime_region.base = (void*)elf_load_address;
    g_runtime_region.page_count = SIZE_TO_PAGES(elf_top_address - elf_load_address);
    CHECK(region_reserve_static(&g_user_memory, &g_runtime_region, 0));

    // now actually map it
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

        // get the data
        CHECK(phdr->p_memsz >= phdr->p_filesz);
        void* data = ELF_ARR(char, phdr->p_filesz, phdr->p_offset);

        // align to page
        uintptr_t vaddr = ALIGN_DOWN(phdr->p_vaddr, PAGE_SIZE);
        size_t top_address;
        CHECK(!__builtin_add_overflow(phdr->p_vaddr, phdr->p_memsz, &top_address));
        top_address = ALIGN_UP(top_address, PAGE_SIZE);
        size_t aligned_size = top_address - vaddr;

        // setup the region
        region_t* region = region_allocate(
            &g_runtime_region,
            SIZE_TO_PAGES(aligned_size), 0,
            (void*)vaddr
        );
        CHECK_ERROR(region != NULL, ERROR_OUT_OF_MEMORY);
        region->name = name;
        region->pinned = true;

        // copy the data, we need to enable accessing user memory while we do that
        asm("stac");
        memset((void*)phdr->p_vaddr, 0, SIZE_TO_PAGES(phdr->p_memsz));
        if (phdr->p_filesz != 0) {
            memcpy((void*)phdr->p_vaddr, data, phdr->p_filesz);
        }
        asm("clac");
    }

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

        // set the correct protections now
        if (protection != MAPPING_PROTECTION_RW) {
            mapping_protect((void*)ALIGN_DOWN(phdr->p_vaddr, PAGE_SIZE), protection);
        }

        // TODO: lock region
    }

    // lock the runtime region itself
    g_runtime_region.locked = true;

    // and finally create the usermode thread and start it
    thread_t* runtime_init_thread = nullptr;
    RETHROW(user_thread_create(&runtime_init_thread, (void*)ehdr->e_entry, NULL, "runtime-init"));
    scheduler_wakeup_thread(runtime_init_thread);

cleanup:
    return err;
}
