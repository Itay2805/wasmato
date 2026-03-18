#include "function.h"

#include "lib/printf.h"
#include "lib/stb_ds.h"
#include "wasm/buffer.h"

typedef struct jit_function_ctx {
    err_t err;
    jit_context_t* ctx;
    uint32_t funcidx;
} jit_function_ctx_t;

typedef struct jit_block {
    spidir_block_t block;
} jit_block_t;

typedef struct jit_value {
    spidir_value_t value;
    spidir_value_type_t type;
} jit_value_t;

#define JIT_PUSH(_type, _value) \
    do { \
        jit_value_t jit_value__ = { \
            .value = _value, \
            .type = _type \
        }; \
        arrpush(stack, jit_value__); \
    } while (0)

#define JIT_POP(_type) \
    ({ \
        spidir_value_type_t type__ = _type; \
        CHECK(arrlen(stack) >= 1); \
        jit_value_t jit_value__ = arrpop(stack); \
        CHECK(jit_value__.type == type__, "%d != %d", jit_value__.type, type__); \
        jit_value__.value; \
    })

static spidir_value_type_t jit_get_spidir_value_type(wasm_value_type_t type) {
    switch (type) {
        case WASM_VALUE_TYPE_F64: return SPIDIR_TYPE_F64;
        case WASM_VALUE_TYPE_F32: return SPIDIR_TYPE_F32;
        case WASM_VALUE_TYPE_I64: return SPIDIR_TYPE_I64;
        case WASM_VALUE_TYPE_I32: return SPIDIR_TYPE_I32;
        default: ASSERT(!"Invalid wasm type");
    }
}

err_t jit_prepare_function(jit_context_t* ctx, uint32_t funcidx) {
    err_t err = NO_ERROR;
    spidir_value_type_t* args = nullptr;

    if (ctx->functions[funcidx].inited) {
        goto cleanup;
    }

    CHECK(funcidx < arrlen(ctx->module->functions));
    typeidx_t typeidx = ctx->module->functions[funcidx];
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

    ctx->functions[funcidx].spidir = spidir_module_create_function(
        ctx->spidir,
        name,
        ret_type,
        arrlen(args), args
    );

    arrpush(ctx->queue, funcidx);

    ctx->functions[funcidx].inited = true;

    cleanup:
        arrfree(args);

    return err;
}

static void jit_build_function(spidir_builder_handle_t builder, void* _ctx) {
    err_t err = NO_ERROR;

    // the exec stack
    jit_value_t* stack = nullptr;

    // the general context
    jit_function_ctx_t* context = _ctx;
    uint32_t funcidx = context->funcidx;
    jit_context_t* ctx = context->ctx;

    wasm_type_t* type = &ctx->module->types[ctx->module->functions[funcidx]];

    // the code buffer
    buffer_t code = {
        .data = ctx->module->code[funcidx].code,
        .len = ctx->module->code[funcidx].length
    };

    // TODO: setup locals
    uint32_t locals_count = BUFFER_PULL_U32(&code);
    CHECK(locals_count == 0);

    // setup the entry block
    spidir_block_t block = spidir_builder_create_block(builder);
    spidir_builder_set_entry_block(builder, block);
    spidir_builder_set_block(builder, block);

    while (code.len != 0) {
        uint8_t byte = BUFFER_PULL(uint8_t, &code);
        switch (byte) {
            //----------------------------------------------------------------------------------------------------------
            // Numeric instructions
            //----------------------------------------------------------------------------------------------------------

            case 0x41: {
                int32_t value = BUFFER_PULL_I32(&code);
                JIT_PUSH(SPIDIR_TYPE_I32, spidir_builder_build_iconst(
                    builder,
                    SPIDIR_TYPE_I32, value
                ));
            } break;

            case 0x42: {
                int64_t value = BUFFER_PULL_I64(&code);
                JIT_PUSH(SPIDIR_TYPE_I64, spidir_builder_build_iconst(
                    builder,
                    SPIDIR_TYPE_I64, value
                ));
            } break;

            case 0x43: {
                float value = BUFFER_PULL(float, &code);
                JIT_PUSH(SPIDIR_TYPE_F32, spidir_builder_build_fconst32(builder, value));
            } break;

            case 0x44: {
                double value = BUFFER_PULL(double, &code);
                JIT_PUSH(SPIDIR_TYPE_F64, spidir_builder_build_fconst64(builder, value));
            } break;

            //----------------------------------------------------------------------------------------------------------
            // Expression end
            //----------------------------------------------------------------------------------------------------------

            case 0x0B: {
                CHECK(arrlen(stack) == arrlen(type->func.result_types));
                if (arrlen(stack) == 0) {
                    CHECK(arrlen(stack) == 0);
                    spidir_builder_build_return(builder, SPIDIR_VALUE_INVALID);
                } else {
                    CHECK(arrlen(stack) == 1);
                    spidir_value_type_t ret_type = jit_get_spidir_value_type(type->func.result_types[0]);
                    spidir_builder_build_return(builder, JIT_POP(ret_type));
                }
            } break;

            default: CHECK_FAIL("%02x", byte);
        }
    }


cleanup:
    arrfree(stack);

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
    spidir_module_build_function(ctx->spidir, function->spidir, jit_build_function, &context);
    RETHROW(context.err);

cleanup:
    return err;
}
