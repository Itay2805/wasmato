#include "jit.h"

#include "function.h"
#include "jit_internal.h"

#include "lib/printf.h"
#include "lib/stb_ds.h"
#include "spidir/log.h"

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

void wasm_jit_init(void) {
    spidir_log_init(spidir_log_callback);
    spidir_log_set_max_level(SPIDIR_LOG_LEVEL_DEBUG);
}

static spidir_dump_status_t spidir_dump_callback(const char* data, size_t size, void* ctx) {
    debug_print("%.*s", (int)size, data);
    return SPIDIR_DUMP_CONTINUE;
}

err_t wasm_jit_module(wasm_module_t* module) {
    err_t err = NO_ERROR;
    jit_context_t ctx = {
        .module = module
    };

    // it should be cheap enough to allocate it linearly
    arrsetlen(ctx.functions, arrlen(module->functions));
    memset(ctx.functions, 0, sizeof(*ctx.functions) * arrlen(ctx.functions));

    // setup the jit
    ctx.spidir = spidir_module_create();

    // add the entry point as the first function we jit
    wasm_export_t* export = wasm_find_export(module, "_start");
    CHECK(export != nullptr);
    CHECK(export->kind == WASM_EXPORT_FUNC);
    RETHROW(jit_prepare_function(&ctx, export->index));

    // and now go over everything
    while (arrlen(ctx.queue) != 0) {
        uint32_t funcidx = arrpop(ctx.queue);
        RETHROW(jit_function(&ctx, funcidx));
    }

    spidir_module_dump(ctx.spidir, spidir_dump_callback, nullptr);

cleanup:
    if (ctx.spidir != nullptr) {
        spidir_module_destroy(ctx.spidir);
    }
    arrfree(ctx.queue);
    arrfree(ctx.functions);

    return err;
}