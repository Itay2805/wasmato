#include "function.h"

#include "inst.h"
#include "opcodes.h"
#include "lib/printf.h"
#include "lib/stb_ds.h"
#include "wasm/buffer.h"

typedef struct jit_function_ctx {
    err_t err;
    jit_context_t* ctx;
    uint32_t funcidx;
} jit_function_ctx_t;

err_t jit_prepare_function(jit_context_t* ctx, uint32_t funcidx) {
    err_t err = NO_ERROR;
    spidir_value_type_t* args = nullptr;

    size_t imports_count = arrlen(ctx->module->imports);
    CHECK(funcidx < arrlen(ctx->module->functions) + imports_count);

    if (ctx->functions[funcidx].inited) {
        goto cleanup;
    }

    typeidx_t typeidx;
    if (funcidx < imports_count) {
        typeidx = ctx->module->imports[funcidx].index;
    } else {
        typeidx = ctx->module->functions[funcidx - imports_count];
    }
    wasm_type_t* type = &ctx->module->types[typeidx];
    CHECK(type->kind == WASM_TYPE_KIND_FUNC);

    // the ret type
    spidir_value_type_t ret_type = SPIDIR_TYPE_NONE;
    if (arrlen(type->func.result_types) == 1) {
        ret_type = jit_get_spidir_value_type(type->func.result_types[0]);
    } else {
        CHECK(arrlen(type->func.result_types) == 0);
    }

    // the args, note that we add two hidden parameters
    // which are the globals and memory base
    arrsetcap(args, arrlen(type->func.arg_types) + 2);

    arrpush(args, SPIDIR_TYPE_PTR); // the memory base
    arrpush(args, SPIDIR_TYPE_PTR); // the globals base

    for (int i = 0; i < arrlen(type->func.arg_types); i++) {
        arrpush(args, jit_get_spidir_value_type(type->func.arg_types[i]));
    }

    char name[64];
    snprintf_(name, sizeof(name), "func%d", funcidx);

    if (funcidx < arrlen(ctx->module->imports)) {
        // for imports we create an extern reference
        spidir_extern_function_t func = spidir_module_create_extern_function(
            ctx->spidir,
            name,
            ret_type,
            arrlen(args), args
        );
        ctx->functions[funcidx].spidir = spidir_funcref_make_external(func);
    } else {
        // for normal function create as internal function
        spidir_function_t func = spidir_module_create_function(
            ctx->spidir,
            name,
            ret_type,
            arrlen(args), args
        );
        ctx->functions[funcidx].spidir = spidir_funcref_make_internal(func);

        // only queue internal functions for jitting
        arrpush(ctx->queue, funcidx);
    }

    ctx->functions[funcidx].inited = true;

cleanup:
    arrfree(args);

    return err;
}

#define JIT_TRACE(fmt, ...) \
    do { \
        TRACE("wasm: \t" fmt, ##__VA_ARGS__); \
    } while (0)

static void jit_print_opcode(uint8_t opcode) {
    if (g_wasm_opcode_names[opcode] == nullptr) {
        JIT_TRACE("<unknown 0x%02x>", opcode);
    } else {
        JIT_TRACE("%s", g_wasm_opcode_names[opcode]);
    }
}

typedef struct wasm_mem_arg {
    uint32_t index;
    uint32_t align;
    uint64_t offset;
} wasm_mem_arg_t;

static err_t jit_pull_memarg(buffer_t* buffer, wasm_mem_arg_t* arg) {
    err_t err = NO_ERROR;

    arg->align = BUFFER_PULL_U32(buffer);

    // if the value is larger than or equal to 64 then we have an index
    // and the real value is 64 less
    if (arg->align >= 64) {
        arg->align -= 64;
        arg->index = BUFFER_PULL_U32(buffer);
    } else {
        arg->index = 0;
    }

    // validate the final alignment
    CHECK(arg->align < 64);

    // get the offset
    arg->offset = BUFFER_PULL_U64(buffer);

cleanup:
    return err;
}

static void jit_build_function(spidir_builder_handle_t builder, void* _ctx) {
    err_t err = NO_ERROR;

    // the general context
    jit_function_ctx_t* context = _ctx;
    uint32_t funcidx = context->funcidx;
    jit_context_t* ctx = context->ctx;
    jit_instruction_ctx_t inst = {};

    TRACE("wasm: func%d", funcidx);

    // this must be an internal function, verify as such
    size_t imports_count = arrlen(ctx->module->imports);
    CHECK(funcidx >= imports_count);
    funcidx -= imports_count;
    typeidx_t typeidx = ctx->module->functions[funcidx];
    wasm_type_t* type = &ctx->module->types[typeidx];
    CHECK(type->kind == WASM_TYPE_KIND_FUNC);

    // the code buffer
    buffer_t code = {
        .data = ctx->module->code[funcidx].code,
        .len = ctx->module->code[funcidx].length
    };

    // the main block
    jit_label_t label = {};

    // setup params
    wasm_value_type_t* arg_types = type->func.arg_types;
    arrsetlen(inst.locals_types, arrlen(arg_types));
    arrsetlen(label.locals_values, arrlen(arg_types));
    for (int i = 0; i < arrlen(arg_types); i++) {
        inst.locals_types[i] = jit_get_spidir_value_type(arg_types[i]);

        // offset of 2 because first two params are the locals and memory base
        label.locals_values[i] = spidir_builder_build_param_ref(builder, i + 2);
    }

    // setup ret type
    CHECK(arrlen(type->func.result_types) <= 1);
    if (arrlen(type->func.result_types) == 1) {
        inst.ret_type = jit_get_spidir_value_type(type->func.result_types[0]);
    } else {
        inst.ret_type = SPIDIR_TYPE_NONE;
    }

    // setup locals
    uint32_t locals_count = BUFFER_PULL_U32(&code);
    for (int i = 0; i < locals_count; i++) {
        uint32_t count = BUFFER_PULL_U32(&code);

        wasm_value_type_t type = 0;
        RETHROW(buffer_pull_val_type(&code, &type));

        spidir_value_type_t* types = arraddnptr(inst.locals_types, count);
        spidir_value_t* values = arraddnptr(label.locals_values, count);
        for (int j = 0; j < count; j++) {
            types[j] = jit_get_spidir_value_type(type);
            // TODO: do we need to initialize these to zero or nah?
            values[j] = SPIDIR_VALUE_INVALID;
        }
    }

    // setup the entry block
    spidir_block_t block = spidir_builder_create_block(builder);
    spidir_builder_set_entry_block(builder, block);
    spidir_builder_set_block(builder, block);
    arrpush(inst.labels, label);

    // jit everything
    while (code.len != 0) {
        // ensure we have a label currently
        CHECK(arrlen(inst.labels) > 0);

        // get the next opcode
        uint8_t byte = BUFFER_PULL(uint8_t, &code);
        jit_instruction_t callback = g_wasm_inst_jit_callbacks[byte];
        CHECK(callback != nullptr, "Unknown wasm opcode %02x", byte);

        // emit it
        RETHROW(callback(builder, &code, ctx, &inst, &arrlast(inst.labels)));
    }

    // ensure we have no more labels left
    CHECK(arrlen(inst.labels) == 0);

cleanup:
    // freeup everything
    while (arrlen(inst.labels) != 0) {
        jit_label_t label = arrpop(inst.labels);
        jit_free_label(&label);
    }
    arrfree(inst.labels);
    arrfree(inst.locals_types);

    context->err = err;
}

err_t jit_function(jit_context_t* ctx, uint32_t funcidx) {
    err_t err = NO_ERROR;

    jit_function_t* function = &ctx->functions[funcidx];
    CHECK(function->inited);

    jit_function_ctx_t context = {
        .err = NO_ERROR,
        .funcidx = funcidx,
        .ctx = ctx
    };
    CHECK(spidir_funcref_is_internal(function->spidir));
    spidir_module_build_function(
        ctx->spidir,
        spidir_funcref_get_internal(function->spidir),
        jit_build_function,
        &context
    );
    RETHROW(context.err);

cleanup:
    return err;
}
