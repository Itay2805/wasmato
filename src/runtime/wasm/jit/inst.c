//
// Created by tomato on 13/04/2026.
//

#include "inst.h"

#include "function.h"
#include "lib/stb_ds.h"

#define JIT_PUSH(_type, _value) \
    do { \
        jit_value_t value = { \
            .type = _type, \
            .value = _value, \
        }; \
        arrpush(label->stack, value); \
    } while (0)

#define JIT_POP(_type) \
    ({ \
        CHECK(arrlen(label->stack) >= 1); \
        jit_value_t value__ = arrpop(label->stack); \
        CHECK(value__.type == _type); \
        value__.value; \
    })

void jit_free_label(jit_label_t* label) {
    arrfree(label->locals_phis);
    arrfree(label->locals_values);
    arrfree(label->stack);
}

static wasm_type_t* wasm_get_func_type(jit_context_t* ctx, uint32_t funcidx) {
    size_t imports_count = arrlen(ctx->module);
    typeidx_t functype;
    if (funcidx < imports_count) {
        functype = ctx->module->functions[funcidx];
    } else {
        functype = ctx->module->functions[funcidx - imports_count];
    }
    wasm_type_t* type = &ctx->module->types[functype];
    if (type->kind != WASM_TYPE_KIND_FUNC) return nullptr;
    return type;
}

//----------------------------------------------------------------------------------------------------------------------
// Parametric Instructions
//----------------------------------------------------------------------------------------------------------------------

static err_t jit_wasm_unreachable(spidir_builder_handle_t builder, buffer_t* code, jit_context_t* ctx, jit_instruction_ctx_t* inst, jit_label_t* label) {
    err_t err = NO_ERROR;

    TRACE("wasm: \tunreachable");
    spidir_builder_build_unreachable(builder);
    label->terminated = true;

cleanup:
    return err;
}

//----------------------------------------------------------------------------------------------------------------------
// Control Instructions
//----------------------------------------------------------------------------------------------------------------------

static err_t jit_wasm_call(spidir_builder_handle_t builder, buffer_t* code, jit_context_t* ctx, jit_instruction_ctx_t* inst, jit_label_t* label) {
    err_t err = NO_ERROR;
    spidir_value_t* params = nullptr;

    // prepare the function for jitting
    uint32_t funcidx = BUFFER_PULL_U32(code);
    TRACE("wasm: \tcall %d", funcidx);

    RETHROW(jit_prepare_function(ctx, funcidx));
    jit_function_t* callee = &ctx->functions[funcidx];

    // get the function signature
    wasm_type_t* type = wasm_get_func_type(ctx, funcidx);
    CHECK(type != nullptr);

    // first two params are the membase and locals
    arrsetcap(params, arrlen(type->func.arg_types) + 2);
    arrpush(params, spidir_builder_build_param_ref(builder, 0));
    arrpush(params, spidir_builder_build_param_ref(builder, 1));

    // pop the rest of the args from the stack
    for (int i = 0; i < arrlen(type->func.arg_types); i++) {
        spidir_value_t value = JIT_POP(type->func.arg_types[i]);
        arrpush(params, value);
    }

    // perform the call
    spidir_value_t ret_val = spidir_builder_build_call(builder, callee->spidir, arrlen(params), params);

    // if we have a return push it into the stack
    if (arrlen(type->func.result_types) != 0) {
        CHECK(arrlen(type->func.result_types) == 1);

        JIT_PUSH(
            jit_get_spidir_value_type(type->func.result_types[0]),
            ret_val
        );
    }

cleanup:
    arrfree(params);

    return err;
}

//----------------------------------------------------------------------------------------------------------------------
// Variable Instructions
//----------------------------------------------------------------------------------------------------------------------

static err_t jit_wasm_local_get(spidir_builder_handle_t builder, buffer_t* code, jit_context_t* ctx, jit_instruction_ctx_t* inst, jit_label_t* label) {
    err_t err = NO_ERROR;

    uint32_t index = BUFFER_PULL_U32(code);
    TRACE("wasm: \tlocal.get %d", index);
    CHECK(index < arrlen(inst->locals_types));
    JIT_PUSH(inst->locals_types[index], label->locals_values[index]);

cleanup:
    return err;
}

//----------------------------------------------------------------------------------------------------------------------
// Instruction lookup table
//----------------------------------------------------------------------------------------------------------------------

static err_t jit_wasm_end(spidir_builder_handle_t builder, buffer_t* code, jit_context_t* ctx, jit_instruction_ctx_t* inst, jit_label_t* label) {
    err_t err = NO_ERROR;

    TRACE("wasm: \tend");

    if (!label->terminated) {
        // block not terminated, we have things to do

        // TODO: handle this case
        CHECK(!label->loop);

        if (arrlen(inst->labels) == 1) {
            // this is the last block, we need to handle
            // a return sequence
            spidir_value_t value = SPIDIR_VALUE_INVALID;
            if (inst->ret_type != SPIDIR_TYPE_NONE) {
                value = JIT_POP(inst->ret_type);
            }
            CHECK(arrlen(label->stack) == 0);
            spidir_builder_build_return(builder, value);

        } else {
            CHECK_FAIL();
        }
    }

    // remove the block
    label = nullptr;
    jit_label_t top_label = arrpop(inst->labels);
    jit_free_label(&top_label);

cleanup:
    return err;
}

const jit_instruction_t g_wasm_inst_jit_callbacks[0x100] = {
    [0x0B] = jit_wasm_end,

    // Parametric Instructions
    [0x00] = jit_wasm_unreachable,

    // Control Instructions
    [0x10] = jit_wasm_call,

    // Variable Instructions
    [0x20] = jit_wasm_local_get,
};
