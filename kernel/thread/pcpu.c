#include "pcpu.h"

#include "arch/intrin.h"
#include "lib/printf.h"
#include "lib/string.h"
#include "mem/vmar.h"
#include "mem/vmars.h"
#include "mem/vmo.h"
#include "mem/kernel/alloc.h"

extern char __start_pcpu_data[];
extern char __stop_pcpu_data[];

/**
 * The id of the current cpu
 */
static CPU_LOCAL int m_cpu_id;

/**
 * The fsbase of the current cpu
 */
static CPU_LOCAL size_t m_cpu_fs_base;

static CPU_LOCAL char m_pcpu_mapping_name[sizeof("pcpu-") + 11];

/**
 * All the fs-bases of all the cores
 */
static uintptr_t* m_all_fs_bases;

void init_early_pcpu(void) {
    // the BSP uses offset zero, because the per-cpu variables are
    // allocated inside of the kernel, it means that the BSP can
    // just use that allocation without worrying
    __wrmsr(MSR_IA32_FS_BASE, 0);
    m_cpu_id = 0;
    m_cpu_fs_base = 0;
}

err_t init_pcpu(int cpu_count) {
    err_t err = NO_ERROR;

    // TODO: check if TSC deadline is supported, if so use it, otherwise use
    //       lapic timer or whatever else we want
    m_all_fs_bases = alloc_array(uintptr_t, cpu_count);
    CHECK_ERROR(m_all_fs_bases != NULL, ERROR_OUT_OF_MEMORY);

    // the BSP is always at offset zero
    m_all_fs_bases[0] = 0;

    // setup the rest of the cores
    size_t pcpu_size = ALIGN_UP(__stop_pcpu_data - __start_pcpu_data, PAGE_SIZE);
    for (int i = 1; i < cpu_count; i++) {
        // allocate and map the pcpu data
        vmo_t* vmo = vmo_create(pcpu_size);
        CHECK_ERROR(vmo != NULL, ERROR_OUT_OF_MEMORY);
        void* mapped_addr = NULL;
        RETHROW(vmar_map(
            &g_upper_half_vmar,
            VMAR_MAP_WRITE | VMAR_MAP_POPULATE, 0,
            vmo, 0, pcpu_size, 0,
            &mapped_addr
        ));

        // remember the offset
        m_all_fs_bases[i] = mapped_addr - (void*)__start_pcpu_data;

        // and set the name
        char* name = pcpu_get_pointer_of(&m_pcpu_mapping_name, i);
        snprintf_(name, sizeof(m_pcpu_mapping_name) - 1, "pcpu-%d", i);
        vmo->object.name = name;
    }

cleanup:
    return err;
}

err_t pcpu_init_per_core(int cpu_id) {
    err_t err = NO_ERROR;

    // set the offset
    size_t offset = m_all_fs_bases[cpu_id];
    __wrmsr(MSR_IA32_FS_BASE, offset);

    // setup the cpu id and fs base of the current cpu
    m_cpu_id = cpu_id;
    m_cpu_fs_base = offset;

cleanup:
    return err;
}

int get_cpu_id() {
    return m_cpu_id;
}

void* pcpu_get_pointer(__seg_fs void* ptr) {
    return (void*)(m_cpu_fs_base + (uintptr_t)ptr);
}

void* pcpu_get_pointer_of(__seg_fs void* ptr, int cpu_id) {
    return (void*)(m_all_fs_bases[cpu_id] + (uintptr_t)ptr);
}
