#include "jit.h"

#include "function.h"
#include "jit_internal.h"

#include "lib/stb_ds.h"
#include "lib/unaligned.h"
#include "spidir/log.h"
#include "spidir/codegen.h"
#include "spidir/x64.h"
#include "spidir/opt.h"
#include "uapi/page.h"
#include "lib/syscall.h"

static spidir_codegen_machine_handle_t g_spidir_machine = nullptr;

static void spidir_log_callback(spidir_log_level_t level, const char* module, size_t module_len, const char* message, size_t message_len) {
    switch (level) {
        default:
        case SPIDIR_LOG_LEVEL_TRACE: DEBUG("%.*s: %.*s", (int)module_len, module, (int)message_len, message); break;
        case SPIDIR_LOG_LEVEL_DEBUG: DEBUG("%.*s: %.*s", (int)module_len, module, (int)message_len, message); break;
        case SPIDIR_LOG_LEVEL_INFO: TRACE("%.*s: %.*s", (int)module_len, module, (int)message_len, message); break;
        case SPIDIR_LOG_LEVEL_WARN: WARN("%.*s: %.*s", (int)module_len, module, (int)message_len, message); break;
        case SPIDIR_LOG_LEVEL_ERROR: ERROR("%.*s: %.*s", (int)module_len, module, (int)message_len, message); break;
    }
}

static int isprint(int c) {
    return (unsigned)c - 0x20 < 0x5f;
}

static void hexdump(const void *ptr, size_t buflen) {
    const unsigned char *buf = (const unsigned char*)ptr;
    for (size_t i = 0; i < buflen; i += 16) {
        // 1. Print the offset
        debug_print("%06zx: ", i);

        // 2. Print hexadecimal byte values
        for (size_t j = 0; j < 16; j++) {
            if (i + j < buflen) {
                debug_print("%02x ", buf[i + j]);
            } else {
                debug_print("   ");
            }
        }

        // 3. Print printable ASCII characters
        debug_print(" |");
        for (int j = 0; j < 16; j++) {
            if (i + j < buflen) {
                unsigned char c = buf[i + j];
                debug_print("%c", isprint(c) ? c : '.');
            }
        }
        debug_print("|\n");
    }
}

void wasm_jit_init(void) {
    spidir_log_init(spidir_log_callback);
    spidir_log_set_max_level(SPIDIR_LOG_LEVEL_DEBUG);

    spidir_x64_machine_config_t machine_config = {
        .extern_code_model = SPIDIR_X64_CM_LARGE_ABS,
        .internal_code_model = SPIDIR_X64_CM_SMALL_PIC,
    };
    g_spidir_machine = spidir_codegen_create_x64_machine_with_config(&machine_config);
}

void wasm_jit_free(wasm_jit_t* jit) {
    if (jit->code != nullptr) {
        sys_jit_free(jit->code);
        jit->code = nullptr;
    }
    shfree(jit->exported_functions);
}

static spidir_dump_status_t spidir_dump_callback(const char* data, size_t size, void* ctx) {
    debug_print("%.*s", (int)size, data);
    return SPIDIR_DUMP_CONTINUE;
}

static err_t jit_emit_spidir(jit_context_t* ctx) {
    err_t err = NO_ERROR;

    // add all the exported functions into the functions
    // that we want to jit
    for (int i = 0; i < shlen(ctx->module->exports); i++) {
        wasm_export_t* export = &ctx->module->exports[i];
        if (export->kind == WASM_EXPORT_FUNC) {
            RETHROW(jit_prepare_function(ctx, export->index));
        }
    }

    // prepare the IR for everything
    while (arrlen(ctx->queue) != 0) {
        uint32_t funcidx = arrpop(ctx->queue);
        RETHROW(jit_function(ctx, funcidx));
    }

cleanup:
    return err;
}

static inline uint8_t* jit_align(uint8_t* ptr, size_t alignment, bool code) {
    size_t length = arrlen(ptr);
    size_t unaligned = (length % alignment);
    if (unaligned != 0) {
        size_t to_add = alignment - unaligned;
        uint8_t* data = arraddnptr(ptr, to_add);
        if (code) {
            memset(data, 0xCC, to_add);
        } else {
            memset(data, 0x00, to_add);
        }
    }
    return ptr;
}

static inline uint8_t* jit_push(uint8_t* ptr, const void* data, size_t length) {
    uint8_t* data_ptr = arraddnptr(ptr, length);
    memcpy(data_ptr, data, length);
    return ptr;
}

static inline uint8_t* jit_pad(uint8_t* ptr, size_t length) {
    void* data_ptr = arraddnptr(ptr, length);
    memset(data_ptr, 0xCC, length);
    return ptr;
}

static err_t jit_apply_reloc(uint8_t* code, size_t code_size, spidir_reloc_kind_t kind, size_t offset, int64_t addened, void* target) {
    err_t err = NO_ERROR;

    void* F = code + offset;
    void* P = target;

    switch (kind) {
        case SPIDIR_RELOC_X64_PC32: {
            CHECK(offset + 4 <= code_size);
            ptrdiff_t value = F + addened - P;

            // ensure within signed 32bit range
            CHECK(INT32_MIN <= value);
            CHECK(value <= INT32_MAX);

            // place it
            POKE(uint32_t, F) = value;
        } break;

        case SPIDIR_RELOC_X64_ABS64: {
            CHECK(offset + 8 <= code_size);
            POKE(uint64_t, F) = (uintptr_t)F + addened;
        } break;

        default:
            CHECK_FAIL();
    }

cleanup:
    return err;
}

static void* jit_get_indirect(void* func) {
    func -= 4;

    // place an endbr64
    // TODO: ignore when IBT is not supported
    uint8_t* opcode = func;
    opcode[0] = 0xF3;
    opcode[1] = 0x0F;
    opcode[2] = 0x1E;
    opcode[3] = 0xFA;

    return func;
}

static err_t jit_emit_code(jit_context_t* ctx, wasm_jit_t* jit) {
    err_t err = NO_ERROR;

    spidir_codegen_blob_handle_t* blobs = nullptr;
    spidir_function_t* functions = nullptr;

    // maps
    struct code_map_entry {
        spidir_function_t key;
        size_t code_offset;
        size_t constpool_offset;
    }* code_map = nullptr;

    spidir_codegen_config_t codgen_config = {
        .verify_ir = true,
        .verify_regalloc = true
    };

    uint8_t* code = nullptr;
    uint8_t* rodata = nullptr;

    // TODO: generate functions lazily based on relocations
    //       so once spidir can inline we only generate whatever
    //       that is needed (we already only generate functions that
    //       are called at the wasm level)

    // now go over and jit everything
    for (int i = 0; i < arrlen(ctx->functions); i++) {
        if (!ctx->functions[i].inited) {
            continue;
        }
        spidir_funcref_t funcref = ctx->functions[i].spidir;
        if (!spidir_funcref_is_internal(funcref)) {
            continue;
        }
        spidir_function_t func = spidir_funcref_get_internal(funcref);

        // actually emit the function
        spidir_codegen_blob_handle_t blob;
        spidir_codegen_status_t status = spidir_codegen_emit_function(
            g_spidir_machine,
            &codgen_config,
            ctx->spidir, func,
            &blob
        );
        CHECK(status == SPIDIR_CODEGEN_OK, "%d", status);

        // push the blob and the function next to
        // it for easier lookups later
        arrpush(blobs, blob);
        arrpush(functions, func);

        //
        // constpool
        //

        rodata = jit_align(rodata,
            spidir_codegen_blob_get_constpool_align(blob),
            false
        );

        size_t rodata_offset = arrlen(rodata);

        rodata = jit_push(rodata,
            spidir_codegen_blob_get_constpool(blob),
            spidir_codegen_blob_get_constpool_size(blob)
        );

        //
        // code
        //

        // we align the code and pad it with 16 bytes, this
        // place will also be used for indirect jumps
        code = jit_align(code, 16, true);
        code = jit_pad(code, 16);

        size_t code_offset = arrlen(code);

        code = jit_push(code,
            spidir_codegen_blob_get_code(blob),
            spidir_codegen_blob_get_code_size(blob)
        );

        struct code_map_entry entry = {
            .key = func,
            .code_offset = code_offset,
            .constpool_offset = rodata_offset,
        };
        hmputs(code_map, entry);
    }

    size_t trimmed_code_size = arrlen(code);

    // align everything to page size
    code = jit_align(code, PAGE_SIZE, true);
    rodata = jit_align(rodata, PAGE_SIZE, false);

    // now actually create the mappings
    size_t rx_page_count = SIZE_TO_PAGES(arrlen(code));
    size_t ro_page_count = SIZE_TO_PAGES(arrlen(rodata));
    void* area = sys_jit_alloc(rx_page_count, ro_page_count);
    CHECK(area != nullptr);

    void* jit_code = area;
    void* jit_rodata = area + PAGES_TO_SIZE(rx_page_count);

    memcpy(jit_code, code, arrlen(code));
    memcpy(jit_rodata, rodata, arrlen(rodata));

    // free the temp memory where we built everything
    arrfree(code);
    arrfree(rodata);

    // now go over all the blobs
    // and apply all the relocs
    for (int i = 0; i < arrlen(functions); i++) {
        spidir_function_t func = functions[i];
        spidir_codegen_blob_handle_t blob = blobs[i];
        int code_map_i = hmgeti(code_map, func);
        CHECK(code_map_i >= 0);
        size_t func_code_offset = code_map[code_map_i].code_offset;
        size_t const_pool_offset = code_map[code_map_i].constpool_offset;

        // go over the relocations of the function
        const spidir_codegen_reloc_t* relocs = spidir_codegen_blob_get_relocs(blob);
        for (int j = 0; j < spidir_codegen_blob_get_reloc_count(blob); j++) {
            const spidir_codegen_reloc_t* reloc = &relocs[j];

            // resolve the target
            void* target = nullptr;
            if (reloc->target_kind == SPIDIR_RELOC_TARGET_CONSTPOOL) {
                target = jit_rodata + const_pool_offset;
            } else if (reloc->target_kind == SPIDIR_RELOC_TARGET_INTERNAL_FUNCTION) {
                int callee_i = hmgeti(code_map, reloc->target.internal);
                CHECK(callee_i >= 0);
                target = jit_code + code_map[callee_i].code_offset;

                // we currently expect local functions to only ever have direct accesses
                // indirect functions only exist as part of tables which are initialized
                // in a different place
                CHECK(reloc->target_kind == SPIDIR_RELOC_X64_PC32);
            } else if (reloc->target_kind == SPIDIR_RELOC_TARGET_EXTERNAL_FUNCTION) {
                // TODO: resolve the import...
                target = nullptr;
            } else {
                CHECK_FAIL();
            }

            // actually apply the reloc
            RETHROW(jit_apply_reloc(
                jit_code + func_code_offset,
                spidir_codegen_blob_get_code_size(blob),
                reloc->kind,
                reloc->offset,
                reloc->addend,
                target
            ));
        }
    }

    // fill the export table, this will also emit the endbr64
    sh_new_arena(jit->exported_functions);
    for (int i = 0; i < hmlen(ctx->module->exports); i++) {
        if (ctx->module->exports[i].kind != WASM_EXPORT_FUNC) {
            continue;
        }
        uint32_t funcidx = ctx->module->exports[i].index;
        CHECK(ctx->functions[funcidx].inited);
        spidir_funcref_t funcref = ctx->functions[funcidx].spidir;
        CHECK(spidir_funcref_is_internal(funcref));
        int idx = hmgeti(code_map, spidir_funcref_get_internal(funcref));
        CHECK(idx >= 0);

        // add the exported function as an indirect target
        void* target = jit_get_indirect(jit_code + code_map[idx].code_offset);
        shput(jit->exported_functions, ctx->module->exports[i].key, target);
    }

    // lock it
    sys_jit_lock_protection(area);

    // output the entire thing
    jit->code = area;
    jit->code_page_count = rx_page_count + ro_page_count;

    TRACE("Jitted code:");
    hexdump(area, trimmed_code_size);

cleanup:
    // free the global stuff
    if (IS_ERROR(err)) {
        wasm_jit_free(jit);
    }

    // free the local stuff
    for (int i = 0; i < arrlen(blobs); i++) {
        spidir_codegen_blob_destroy(blobs[i]);
    }
    arrfree(blobs);
    arrfree(functions);
    arrfree(code);
    arrfree(rodata);
    hmfree(code_map);

    return err;
}

static err_t jit_prepare_globals(jit_context_t* ctx, wasm_jit_t* jit) {
    err_t err = NO_ERROR;

    arrsetlen(ctx->globals, arrlen(ctx->module->globals));

    size_t offset = 0;
    for (int i = 0; i < arrlen(ctx->module->globals); i++) {
        wasm_global_t* global = &ctx->module->globals[i];
        spidir_value_type_t type = jit_get_spidir_value_type(global->value.kind);
        ctx->globals[i].type = type;
        if (global->mutable) {
            // mutable, we need space for it
            ctx->globals[i].offset = offset;
            offset += jit_get_spidir_size(type);
        } else {
            // immutable, not going to be allocated, mark it as such
            ctx->globals[i].offset = -1;
        }
    }

    jit->globals_size = offset;

cleanup:
    return err;
}

err_t wasm_jit_module(wasm_module_t* module, wasm_jit_t* jit) {
    err_t err = NO_ERROR;
    jit_context_t ctx = {
        .module = module
    };

    // it should be cheap enough to allocate it linearly
    arrsetlen(ctx.functions, arrlen(module->functions) + arrlen(module->imports));
    memset(ctx.functions, 0, sizeof(*ctx.functions) * arrlen(ctx.functions));

    // setup the globals
    RETHROW(jit_prepare_globals(&ctx, jit));

    ctx.spidir = spidir_module_create();

    // emit the spidir IR
    RETHROW(jit_emit_spidir(&ctx));

    // optimize the module
    spidir_opt_run(ctx.spidir);

    // generate the entire thing
    RETHROW(jit_emit_code(&ctx, jit));

    spidir_module_dump(ctx.spidir, spidir_dump_callback, nullptr);

cleanup:
    if (ctx.spidir != nullptr) {
        spidir_module_destroy(ctx.spidir);
    }
    arrfree(ctx.queue);
    arrfree(ctx.functions);
    arrfree(ctx.globals);

    return err;
}

void* wasm_jit_get_function(wasm_jit_t* module, const char* name) {
    int idx = shgeti(module->exported_functions, name);
    if (idx < 0) {
        return nullptr;
    }
    return module->exported_functions[idx].value;
}