#include "proc.h"

#include <stdatomic.h>
#include <stdint.h>

#include "alloc/alloc.h"

#include "debug/gdb_jit.h"
#include "lib/assert.h"
#include "lib/atomic.h"
#include "lib/defs.h"
#include "lib/except.h"
#include "lib/list.h"
#include "lib/stb_sprintf.h"
#include "lib/syscall.h"

#include "proc/handle.h"
#include "sync/mutex.h"
#include "uapi/page.h"
#include "wasi/wasi.h"
#include "wasm/debug_elf.h"
#include "wasm/jit.h"
#include "wasm/wasm.h"
#include <spidir/x64.h>

#include "thread.h"
#include "wasm_err.h"
#include "wasmato/wasmato.h"

static _Atomic(uint32_t) m_process_id_gen = 0;

static spidir_codegen_machine_handle_t m_spidir_machine_handle;

wasm_proc_t* wasm_get_proc(wasm_proc_t* proc) {
    atomic_fetch_add_explicit(&proc->ref_count, 1, memory_order_acquire);
    return proc;
}

void wasm_put_proc(wasm_proc_t* proc) {
    size_t ref_count = atomic_fetch_sub_explicit(&proc->ref_count, 1, memory_order_release);
    ASSERT(ref_count >= 1);

    if (ref_count == 1) {
        atomic_fence_acquire();

        // free the wasm jit and module
        // TODO: sharing?
        wasm_module_jit_free(&proc->jit);
        wasm_module_free(&proc->module);

        // free the memory itself
        if (proc->memory_base != nullptr) {
            sys_mem_free(proc->memory_base);
        }

        // free the proc itself
        mem_free(proc);
    }
}

wasm_proc_t* wasm_current_proc(void* state_base) {
    wasm_state_t* state = containerof(state_base, wasm_state_t, state);
    return state->proc;
}

static int32_t wasi_thread_spawn(void* memory_base, void* state_base, int32_t start_arg) {
    wasm_proc_t* proc = wasm_current_proc(state_base);

    int32_t tid = 0;
    err_t err = wasm_create_thread(proc, start_arg, &tid);
    switch (err) {
        case NO_ERROR: return tid;
        case ERROR_OUT_OF_MEMORY: return -2;
        default: return -1;
    }
}

static void* wasm_resolve_import(void* arg, const char* module, const char* name, wasm_type_t* type) {
    wasm_proc_t* proc = arg;

    // we only support up to 1 return value always (c code duh)
    if (type->result_types_count > 1) {
        return nullptr;
    }
    
    if (strcmp(module, "wasi") == 0) {
        // standard wasi function
        if (
            strcmp(name, "thread-spawn") == 0 &&
            type->arg_types_count == 1 &&
            type->arg_types[0] == WASM_VALUE_TYPE_I32 &&
            type->result_types_count == 1 &&
            type->result_types[0] == WASM_VALUE_TYPE_I32
        ) {
            // from wasi-threads
            return wasi_thread_spawn;
        } else {
            return nullptr;
        }

    } else if (strcmp(module, "wasi_snapshot_preview1") == 0) {
        // the wasi-p1 compatibility layer
        return wasi_resolve_import(name, type);

    } else if (strcmp(module, "wasmato") == 0) {
        return wasmato_resolve_import(name, proc, type);

    }
    
    return nullptr;
}

err_t wasm_create_proc(
    wasm_proc_type_t type, 
    void* module, size_t module_size,
    proc_handle_t* handles,
    size_t handles_count
) {
    err_t err = NO_ERROR;

    // allocate the new proc
    wasm_proc_t* proc = mem_alloc(sizeof(*proc));
    CHECK_ERROR(proc != nullptr, ERROR_OUT_OF_MEMORY);
    memset(proc, 0, sizeof(*proc));

    proc->type = type;

    // set the pid
    proc->process_id = atomic_fetch_add_explicit(&m_process_id_gen, 1, memory_order_relaxed) + 1;

    // start with ref count of 1
    proc->ref_count = 1;

    // load the module
    RETHROW_WASM(wasm_load_module(&proc->module, module, module_size));

    // the name for the region
    char name[128] = "";
    const char* module_name = "wasm";
    if (proc->module.module_name != nullptr) {
        module_name = proc->module.module_name;
    }
    stbsp_snprintf(name, sizeof(name), "%s#%d", module_name, proc->process_id);

    // allocate the memory, we need to reserve 8gb to ensure that nothing 
    // can accidently overflow or exit the range, from the 8gb only the first
    // 4GB are mappable, because that is all wasm can actually access without 
    // using the static offset in the mapping
    proc->memory_base = sys_mem_reserve(SIZE_TO_PAGES(SIZE_8GB), SIZE_TO_PAGES(SIZE_4GB), name);
    CHECK_ERROR(proc->memory_base != nullptr, ERROR_OUT_OF_MEMORY);

    // perform the initial bump
    proc->memory_size = proc->module.memory.min;
    void* base = sys_mem_bump(proc->memory_base, SIZE_TO_PAGES(proc->module.memory.min));
    CHECK_ERROR(base != nullptr, ERROR_OUT_OF_MEMORY);

    // and initialize the memory
    wasm_module_init_memory(&proc->module, proc->memory_base);

    // setup the machine config the first time we go in here
    if (m_spidir_machine_handle == nullptr) {
        spidir_x64_machine_config_t config = {
            // all functions are within 2GB since that is 
            // the space we reserve for the jit code
            .extern_code_model = SPIDIR_X64_CM_SMALL_PIC,
            .internal_code_model = SPIDIR_X64_CM_SMALL_PIC,

            // we know we support popcount
            .cpu_features.popcnt = 1
        };
        m_spidir_machine_handle = spidir_codegen_create_x64_machine_with_config(&config);
    }

    // jit it 
    wasm_jit_config_t config = {
        // the import resolver
        .resolve_import = wasm_resolve_import,
        .resolve_import_arg = proc,

        // we don't need the debug info
        .emit_debug_info = true,
        .machine_handle = m_spidir_machine_handle,
        
        // please speed
        .optimize = true,
    };
    RETHROW_WASM(wasm_module_jit(&proc->module, &proc->jit, &config));
    
    // for debugging emit an elf file so we can have symbols in gdb
    void* debug_elf_data = nullptr;
    size_t debug_elf_size = 0;
    RETHROW_WASM(wasm_jit_emit_debug_elf(
        &proc->module, &proc->jit, 
        &debug_elf_data, &debug_elf_size
    ));
    gdb_jit_register(debug_elf_data, debug_elf_size);

    // find the wasm entry point, only if sharing is enabled
    if (proc->module.memory.shared) {
        int64_t index = wasm_find_export(&proc->module, "wasi_thread_start");
        if (index >= 0) {
            // get the export
            wasm_export_t* export = &proc->module.exports[index];
            CHECK(export->kind == WASM_EXPORT_FUNC);

            // the func index so we can check it
            wasm_type_t* type = wasm_get_func(&proc->module, export->index);
            CHECK(type != nullptr);
            
            // ensure the signature is correct
            CHECK(type->arg_types_count == 2);
            CHECK(type->arg_types[0] == WASM_VALUE_TYPE_I32);
            CHECK(type->arg_types[1] == WASM_VALUE_TYPE_I32);
            CHECK(type->result_types_count == 0);

            // save it from the jit
            proc->wasi_thread_start = proc->jit.exports[index].func.address;
        }
    }

    // find the entry point (_start)
    int64_t index = wasm_find_export(&proc->module, "_start");
    if (index >= 0) {
        // get the export
        wasm_export_t* export = &proc->module.exports[index];
        CHECK(export->kind == WASM_EXPORT_FUNC);

        // the func index so we can check it
        wasm_type_t* type = wasm_get_func(&proc->module, export->index);
        CHECK(type != nullptr);
        
        // ensure the signature is correct
        CHECK(type->arg_types_count == 0);
        CHECK(type->result_types_count == 0);

        // save it from the jit
        proc->start = proc->jit.exports[index].func.address;
    }

    // install all the default handles
    for (int i = 0; i < handles_count; i++) {
        proc_handle_t handle = handles[i];
        CHECK(handle_table_install(
            &proc->handles, 
            handle.object, 
            handle.rights, 
            handle.fd
        ));
    }

    // and finally create the new thread
    RETHROW(wasm_create_thread(proc, 0, nullptr));

cleanup:
    if (proc != nullptr) {
        wasm_put_proc(proc);
    }

    return err;
}

int32_t wasm_host_memory_size(void* memory_base, void* state_base) {
    wasm_proc_t* proc = wasm_current_proc(state_base);
    return atomic_load_acquire(&proc->memory_size);
}

int32_t wasm_host_memory_grow(void* memory_base, void* state_base, int32_t new_page_count) {
    wasm_proc_t* proc = wasm_current_proc(state_base);

    mutex_lock(&proc->memory_lock);
    
    // the old and new sizes
    size_t current_size = atomic_load_relaxed(&proc->memory_size);
    size_t size_to_add = new_page_count * WASM_PAGE_SIZE;

    // bump the region
    void* result = sys_mem_bump(memory_base, SIZE_TO_PAGES(size_to_add));
    if (result == nullptr) {
        // either out of memory or we went over the page limit
        mutex_unlock(&proc->memory_lock);
        return -1;
    }

    // add the new size
    atomic_fetch_add_explicit(&proc->memory_size, size_to_add, memory_order_release);

    mutex_unlock(&proc->memory_lock);

    // return the page count
    return current_size / WASM_PAGE_SIZE;
}
