#include "inst.h"

#include "function.h"
#include "opcodes.h"
#include "lib/stb_ds.h"

#define JIT_PUSH(_type, _value) \
    do { \
        jit_value_t value__ = { \
            .type = _type, \
            .value = _value, \
        }; \
        arrpush(label->stack, value__); \
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

static err_t jit_wasm_nop(spidir_builder_handle_t builder, buffer_t* code, jit_context_t* ctx, jit_instruction_ctx_t* inst, jit_label_t* label) {
    err_t err = NO_ERROR;

    TRACE("wasm: \tnop");

cleanup:
    return err;
}

//----------------------------------------------------------------------------------------------------------------------
// Control Instructions
//----------------------------------------------------------------------------------------------------------------------

static err_t jit_wasm_pull_block_type(buffer_t* code) {
    err_t err = NO_ERROR;

    // TODO: support for other block types
    uint8_t byte = BUFFER_PULL(uint8_t, code);
    CHECK(byte == 0x40);

cleanup:
    return err;
}

static err_t jit_wasm_block(spidir_builder_handle_t builder, buffer_t* code, jit_context_t* ctx, jit_instruction_ctx_t* inst, jit_label_t* label) {
    err_t err = NO_ERROR;

    RETHROW(jit_wasm_pull_block_type(code));
    TRACE("wasm: \tblock");

    // append a new label
    jit_label_t* new_label = arraddnptr(inst->labels, 1);
    memset(new_label, 0, sizeof(*new_label));

    // this is the block after this block ends
    new_label->block = spidir_builder_create_block(builder);

cleanup:
    return err;
}

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
    CHECK(index < arrlen(inst->locals));
    spidir_value_t value = inst->locals[index].value;
    CHECK(value.id != SPIDIR_VALUE_INVALID.id);
    JIT_PUSH(inst->locals[index].type, value);

cleanup:
    return err;
}

static err_t jit_wasm_local_set(spidir_builder_handle_t builder, buffer_t* code, jit_context_t* ctx, jit_instruction_ctx_t* inst, jit_label_t* label) {
    err_t err = NO_ERROR;

    uint32_t index = BUFFER_PULL_U32(code);
    TRACE("wasm: \tlocal.set %d", index);
    CHECK(index < arrlen(inst->locals));
    inst->locals[index].value = JIT_POP(inst->locals[index].type);

cleanup:
    return err;
}

static err_t jit_wasm_local_tee(spidir_builder_handle_t builder, buffer_t* code, jit_context_t* ctx, jit_instruction_ctx_t* inst, jit_label_t* label) {
    err_t err = NO_ERROR;

    uint32_t index = BUFFER_PULL_U32(code);
    TRACE("wasm: \tlocal.tee %d", index);
    spidir_value_type_t value_type = inst->locals[index].type;
    spidir_value_t value = JIT_POP(value_type);
    inst->locals[index].value = value;
    JIT_PUSH(value_type, value);

cleanup:
    return err;
}

static err_t jit_wasm_global_get(spidir_builder_handle_t builder, buffer_t* code, jit_context_t* ctx, jit_instruction_ctx_t* inst, jit_label_t* label) {
    err_t err = NO_ERROR;

    uint32_t index = BUFFER_PULL_U32(code);
    TRACE("wasm: \tglobal.get %d", index);
    CHECK(index < arrlen(ctx->globals));

    // get the pointer to the global data
    spidir_value_t globals_base = spidir_builder_build_param_ref(builder, 1);
    globals_base = spidir_builder_build_ptroff(builder, globals_base,
        spidir_builder_build_iconst(builder, SPIDIR_TYPE_I64, ctx->globals[index].offset));

    // read it
    spidir_value_type_t value_type = ctx->globals[index].type;
    spidir_value_t value = spidir_builder_build_load(
        builder,
        jit_get_spidir_mem_size(value_type),
        value_type,
        globals_base
    );

    // and push
    JIT_PUSH(value_type, value);

cleanup:
    return err;
}

static err_t jit_wasm_global_set(spidir_builder_handle_t builder, buffer_t* code, jit_context_t* ctx, jit_instruction_ctx_t* inst, jit_label_t* label) {
    err_t err = NO_ERROR;

    uint32_t index = BUFFER_PULL_U32(code);
    TRACE("wasm: \tglobal.set %d", index);
    CHECK(index < arrlen(ctx->globals));

    // get the pointer to the global data
    spidir_value_t globals_base = spidir_builder_build_param_ref(builder, 1);
    globals_base = spidir_builder_build_ptroff(builder, globals_base,
        spidir_builder_build_iconst(builder, SPIDIR_TYPE_I64, ctx->globals[index].offset));

    // read it
    spidir_value_type_t value_type = ctx->globals[index].type;
    spidir_value_t value = JIT_POP(value_type);
    spidir_builder_build_store(
        builder,
        jit_get_spidir_mem_size(value_type),
        value,
        globals_base
    );

cleanup:
    return err;
}


//----------------------------------------------------------------------------------------------------------------------
// Memory instructions
//----------------------------------------------------------------------------------------------------------------------

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

static err_t jit_wasm_load(spidir_builder_handle_t builder, buffer_t* code, jit_context_t* ctx, jit_instruction_ctx_t* inst, jit_label_t* label) {
    err_t err = NO_ERROR;

    uint8_t opcode = ((uint8_t*)code->data)[-1];

    // get the memory argument
    wasm_mem_arg_t mem_arg = {};
    RETHROW(jit_pull_memarg(code, &mem_arg));
    if (mem_arg.index == 0) {
        TRACE("wasm: \t%s %d, %llu", g_wasm_opcode_names[opcode], 1 << mem_arg.align, (unsigned long long)mem_arg.offset);
    } else {
        TRACE("wasm: \t%s %d, %d, %llu", g_wasm_opcode_names[opcode], 1 << mem_arg.align, mem_arg.index, (unsigned long long)mem_arg.offset);
    }

    // figure the exact parameters for the load
    spidir_value_type_t type;
    spidir_mem_size_t mem_size;
    uint32_t sign_extend = 0;
    uint32_t zero_extend = 0;
    switch (opcode) {
        case 0x28: type = SPIDIR_TYPE_I32; mem_size = SPIDIR_MEM_SIZE_4; break;
        case 0x29: type = SPIDIR_TYPE_I64; mem_size = SPIDIR_MEM_SIZE_8; break;
        case 0x2A: type = SPIDIR_TYPE_F32; mem_size = SPIDIR_MEM_SIZE_4; break;
        case 0x2B: type = SPIDIR_TYPE_F64; mem_size = SPIDIR_MEM_SIZE_8; break;
        case 0x2C: type = SPIDIR_TYPE_I32; mem_size = SPIDIR_MEM_SIZE_1; sign_extend = 8; break;
        case 0x2D: type = SPIDIR_TYPE_I32; mem_size = SPIDIR_MEM_SIZE_1; zero_extend = 8; break;
        case 0x2E: type = SPIDIR_TYPE_I32; mem_size = SPIDIR_MEM_SIZE_2; sign_extend = 16; break;
        case 0x2F: type = SPIDIR_TYPE_I32; mem_size = SPIDIR_MEM_SIZE_2; zero_extend = 16; break;
        case 0x30: type = SPIDIR_TYPE_I64; mem_size = SPIDIR_MEM_SIZE_1; sign_extend = 8; break;
        case 0x31: type = SPIDIR_TYPE_I64; mem_size = SPIDIR_MEM_SIZE_1; zero_extend = 8; break;
        case 0x32: type = SPIDIR_TYPE_I64; mem_size = SPIDIR_MEM_SIZE_2; sign_extend = 16; break;
        case 0x33: type = SPIDIR_TYPE_I64; mem_size = SPIDIR_MEM_SIZE_2; zero_extend = 16; break;
        case 0x34: type = SPIDIR_TYPE_I64; mem_size = SPIDIR_MEM_SIZE_4; sign_extend = 32; break;
        case 0x35: type = SPIDIR_TYPE_I64; mem_size = SPIDIR_MEM_SIZE_4; zero_extend = 32; break;
        default: CHECK_FAIL();
    }

    // get the value and offset, the value type depends on the instruction
    spidir_value_t offset = JIT_POP(SPIDIR_TYPE_I32);

    // if we have an offset add it
    if (mem_arg.offset != 0) {
        offset = spidir_builder_build_iadd(builder, offset,
            spidir_builder_build_iconst(builder, SPIDIR_TYPE_I32, mem_arg.offset));
    }

    // extend the offset to 64bit
    offset = spidir_builder_build_iext(builder, offset);
    offset = spidir_builder_build_and(builder, offset,
        spidir_builder_build_iconst(builder, SPIDIR_TYPE_I64, 0xFFFFFFFF));

    // load the value
    spidir_value_t mem_base = spidir_builder_build_param_ref(builder, 0);
    spidir_value_t value = spidir_builder_build_load(
        builder,
        mem_size, type,
        spidir_builder_build_ptroff(builder, mem_base, offset)
    );

    if (sign_extend != 0) {
        // sign extend from the relevant bit
        value = spidir_builder_build_sfill(builder, sign_extend, value);
    } else if (zero_extend != 0) {
        // perform a mask on the lower bits
        uint64_t mask = (1ull << zero_extend) - 1;
        spidir_value_t mask_value = SPIDIR_VALUE_INVALID;
        if (type == SPIDIR_TYPE_I32) {
            mask_value = spidir_builder_build_iconst(builder, SPIDIR_TYPE_I32, mask);
        } else {
            mask_value = spidir_builder_build_iconst(builder, SPIDIR_TYPE_I64, mask);
        }
        value = spidir_builder_build_and(builder, value, mask_value);
    }

    // and finally push it
    JIT_PUSH(type, value);

cleanup:
    return err;
}

static err_t jit_wasm_store(spidir_builder_handle_t builder, buffer_t* code, jit_context_t* ctx, jit_instruction_ctx_t* inst, jit_label_t* label) {
    err_t err = NO_ERROR;

    uint8_t opcode = ((uint8_t*)code->data)[-1];

    // get the memory argument
    wasm_mem_arg_t mem_arg = {};
    RETHROW(jit_pull_memarg(code, &mem_arg));
    if (mem_arg.index == 0) {
        TRACE("wasm: \t%s %d, %llu", g_wasm_opcode_names[opcode], 1 << mem_arg.align, (unsigned long long)mem_arg.offset);
    } else {
        TRACE("wasm: \t%s %d, %d, %llu", g_wasm_opcode_names[opcode], 1 << mem_arg.align, mem_arg.index, (unsigned long long)mem_arg.offset);
    }

    // figure the parameters for the store
    spidir_value_type_t type;
    spidir_mem_size_t mem_size;
    switch (opcode) {
        case 0x36: type = SPIDIR_TYPE_I32; mem_size = SPIDIR_MEM_SIZE_4; break;
        case 0x37: type = SPIDIR_TYPE_I64; mem_size = SPIDIR_MEM_SIZE_8;  break;
        case 0x38: type = SPIDIR_TYPE_F32; mem_size = SPIDIR_MEM_SIZE_4;  break;
        case 0x39: type = SPIDIR_TYPE_F64; mem_size = SPIDIR_MEM_SIZE_8;  break;
        case 0x3A: type = SPIDIR_TYPE_I32; mem_size = SPIDIR_MEM_SIZE_1;  break;
        case 0x3B: type = SPIDIR_TYPE_I32; mem_size = SPIDIR_MEM_SIZE_2;  break;
        case 0x3C: type = SPIDIR_TYPE_I64; mem_size = SPIDIR_MEM_SIZE_1;  break;
        case 0x3D: type = SPIDIR_TYPE_I64; mem_size = SPIDIR_MEM_SIZE_2;  break;
        case 0x3E: type = SPIDIR_TYPE_I64; mem_size = SPIDIR_MEM_SIZE_4;  break;
        default: CHECK_FAIL();
    }

    // get the value and offset, the value type depends on the instruction
    spidir_value_t value = JIT_POP(type);
    spidir_value_t offset = JIT_POP(SPIDIR_TYPE_I32);

    // if we have an offset add it
    if (mem_arg.offset != 0) {
        offset = spidir_builder_build_iadd(builder, offset,
            spidir_builder_build_iconst(builder, SPIDIR_TYPE_I32, mem_arg.offset));
    }

    // extend the offset to 64bit
    offset = spidir_builder_build_iext(builder, offset);
    offset = spidir_builder_build_and(builder, offset,
        spidir_builder_build_iconst(builder, SPIDIR_TYPE_I64, 0xFFFFFFFF));

    // TODO: do something with the alignment

    // this is to ensure we don't actually get anything weird, we are going
    // to reserve 8gb per-instance virtually (with max mapped size of 4GB)
    CHECK(mem_arg.offset <= UINT32_MAX);
    spidir_value_t mem_base = spidir_builder_build_param_ref(builder, 0);
    spidir_builder_build_store(
        builder,
        mem_size,
        value,
        spidir_builder_build_ptroff(builder, mem_base, offset)
    );

cleanup:
    return err;
}

//----------------------------------------------------------------------------------------------------------------------
// Numeric Instructions
//----------------------------------------------------------------------------------------------------------------------

static err_t jit_wasm_i32_const(spidir_builder_handle_t builder, buffer_t* code, jit_context_t* ctx, jit_instruction_ctx_t* inst, jit_label_t* label) {
    err_t err = NO_ERROR;

    int32_t value = BUFFER_PULL_I32(code);
    TRACE("wasm: \ti32.const %d", value);
    JIT_PUSH(SPIDIR_TYPE_I32, spidir_builder_build_iconst(builder, SPIDIR_TYPE_I32, (uint32_t)value));

cleanup:
    return err;
}

static err_t jit_wasm_i64_const(spidir_builder_handle_t builder, buffer_t* code, jit_context_t* ctx, jit_instruction_ctx_t* inst, jit_label_t* label) {
    err_t err = NO_ERROR;

    int64_t value = BUFFER_PULL_I64(code);
    TRACE("wasm: \ti64.const %lld", (long long)value);
    JIT_PUSH(SPIDIR_TYPE_I64, spidir_builder_build_iconst(builder, SPIDIR_TYPE_I64, value));

cleanup:
    return err;
}

static err_t jit_wasm_f32_const(spidir_builder_handle_t builder, buffer_t* code, jit_context_t* ctx, jit_instruction_ctx_t* inst, jit_label_t* label) {
    err_t err = NO_ERROR;

    float value = BUFFER_PULL(float, code);
    TRACE("wasm: \tf32.const %f", value);
    JIT_PUSH(SPIDIR_TYPE_F32, spidir_builder_build_fconst32(builder, value));

cleanup:
    return err;
}

static err_t jit_wasm_f64_const(spidir_builder_handle_t builder, buffer_t* code, jit_context_t* ctx, jit_instruction_ctx_t* inst, jit_label_t* label) {
    err_t err = NO_ERROR;

    double value = BUFFER_PULL(double, code);
    TRACE("wasm: \tf64.const %lf", value);
    JIT_PUSH(SPIDIR_TYPE_F64, spidir_builder_build_fconst64(builder, value));

cleanup:
    return err;
}

static err_t jit_wasm_binopi(spidir_builder_handle_t builder, buffer_t* code, jit_context_t* ctx, jit_instruction_ctx_t* inst, jit_label_t* label) {
    err_t err = NO_ERROR;

    uint8_t opcode = ((uint8_t*)code->data)[-1];
    TRACE("wasm: \t%s", g_wasm_opcode_names[opcode]);

    // figure the exact type
    spidir_value_type_t type;
    switch (opcode) {
        case 0x6A ... 0x73: type = SPIDIR_TYPE_I32; opcode -= 0x6A; break;
        case 0x7C ... 0x85: type = SPIDIR_TYPE_I64; opcode -= 0x7C; break;
        default: CHECK_FAIL();
    }

    // get the two values
    spidir_value_t arg2 = JIT_POP(type);
    spidir_value_t arg1 = JIT_POP(type);

    // and now perform the action
    spidir_value_t value;
    switch (opcode) {
        case 0: value = spidir_builder_build_iadd(builder, arg1, arg2); break;
        case 1: value = spidir_builder_build_isub(builder, arg1, arg2); break;
        case 2: value = spidir_builder_build_imul(builder, arg1, arg2); break;
        case 3: value = spidir_builder_build_sdiv(builder, arg1, arg2); break;
        case 4: value = spidir_builder_build_udiv(builder, arg1, arg2); break;
        case 5: value = spidir_builder_build_srem(builder, arg1, arg2); break;
        case 6: value = spidir_builder_build_urem(builder, arg1, arg2); break;
        case 7: value = spidir_builder_build_and(builder, arg1, arg2); break;
        case 8: value = spidir_builder_build_or(builder, arg1, arg2); break;
        case 9: value = spidir_builder_build_xor(builder, arg1, arg2); break;
        default: CHECK_FAIL();
    }

    // and push it back
    JIT_PUSH(type, value);

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
    [0x01] = jit_wasm_nop,

    // Control Instructions
    [0x02] = jit_wasm_block,
    [0x10] = jit_wasm_call,

    // Variable Instructions
    [0x20] = jit_wasm_local_get,
    [0x21] = jit_wasm_local_set,
    [0x22] = jit_wasm_local_tee,
    [0x23] = jit_wasm_global_get,
    [0x24] = jit_wasm_global_set,

    // Memory Instructions
    [0x28 ... 0x35] = jit_wasm_load,
    [0x36 ... 0x3E] = jit_wasm_store,

    // Numeric Instructions
    [0x41] = jit_wasm_i32_const,
    [0x42] = jit_wasm_i64_const,
    [0x43] = jit_wasm_f32_const,
    [0x44] = jit_wasm_f64_const,
    [0x6A ... 0x73] = jit_wasm_binopi,
    [0x7C ... 0x85] = jit_wasm_binopi,
};
