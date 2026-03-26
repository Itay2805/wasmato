#include "function.h"

#include "opcodes.h"
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

    // the exec stack
    jit_value_t* stack = nullptr;

    // the locals
    jit_value_t* locals = nullptr;

    // the general context
    jit_function_ctx_t* context = _ctx;
    uint32_t funcidx = context->funcidx;
    jit_context_t* ctx = context->ctx;

    TRACE("wasm: func%d", funcidx);

    wasm_type_t* type = &ctx->module->types[ctx->module->functions[funcidx]];
    CHECK(type->kind == WASM_TYPE_KIND_FUNC);

    // the code buffer
    buffer_t code = {
        .data = ctx->module->code[funcidx].code,
        .len = ctx->module->code[funcidx].length
    };

    // the implicit memory base
    spidir_value_t mem_base = spidir_builder_build_param_ref(builder, 0);

    // the implicit globals base
    spidir_value_t globals_base = spidir_builder_build_param_ref(builder, 1);

    // setup params
    wasm_value_type_t* arg_types = type->func.arg_types;
    arrsetlen(locals, arrlen(arg_types));
    for (int i = 0; i < arrlen(arg_types); i++) {
        locals[i].type = jit_get_spidir_value_type(arg_types[i]);
        locals[i].value = spidir_builder_build_param_ref(builder, i + 2);
    }

    // setup locals
    uint32_t locals_count = BUFFER_PULL_U32(&code);
    for (int i = 0; i < locals_count; i++) {
        uint32_t count = BUFFER_PULL_U32(&code);

        wasm_value_type_t type;
        RETHROW(buffer_pull_val_type(&code, &type));

        jit_value_t* values = arraddnptr(locals, count);
        for (int j = 0; j < count; j++) {
            values[j].type = jit_get_spidir_value_type(type);
            values[j].value = SPIDIR_VALUE_INVALID;
        }
    }

    // setup the entry block
    spidir_block_t block = spidir_builder_create_block(builder);
    spidir_builder_set_entry_block(builder, block);
    spidir_builder_set_block(builder, block);

    while (code.len != 0) {
        uint8_t byte = BUFFER_PULL(uint8_t, &code);
        switch (byte) {
            //----------------------------------------------------------------------------------------------------------
            // Variable Instructions
            //----------------------------------------------------------------------------------------------------------

            case 0x20: {
                uint32_t localidx = BUFFER_PULL_U32(&code);
                JIT_TRACE("local.get %d", localidx);
                CHECK(localidx < arrlen(locals));
                JIT_PUSH(locals[localidx].type, locals[localidx].value);
            } break;

            case 0x21: {
                uint32_t localidx = BUFFER_PULL_U32(&code);
                JIT_TRACE("local.set %d", localidx);
                CHECK(localidx < arrlen(locals));

                spidir_value_t value = JIT_POP(locals[localidx].type);
                locals[localidx].value = value;
            } break;

            case 0x22: {
                uint32_t localidx = BUFFER_PULL_U32(&code);
                JIT_TRACE("local.tee %d", localidx);
                CHECK(localidx < arrlen(locals));

                // get the value, set it in the locals array, and also push it
                spidir_value_t value = JIT_POP(locals[localidx].type);
                locals[localidx].value = value;
                JIT_PUSH(locals[localidx].type, value);
            } break;

            case 0x23: {
                uint32_t globalidx = BUFFER_PULL_U32(&code);
                JIT_TRACE("global.get %d", globalidx);

                CHECK(globalidx < arrlen(ctx->globals));
                size_t offset = ctx->globals[globalidx].offset;
                spidir_value_type_t type = ctx->globals[globalidx].type;

                // actually get the value
                spidir_value_t value = SPIDIR_VALUE_INVALID;
                if (offset == -1) {
                    // TODO: load it as a constant
                } else {
                    // load it from memory
                    value = spidir_builder_build_load(
                        builder,
                        jit_get_spidir_mem_size(type), type,
                        spidir_builder_build_ptroff(builder, globals_base,
                            spidir_builder_build_iconst(builder, SPIDIR_TYPE_I32, offset))
                    );
                }

                // push it
                JIT_PUSH(type, value);
            } break;

            case 0x24: {
                uint32_t globalidx = BUFFER_PULL_U32(&code);
                JIT_TRACE("global.set %d", globalidx);

                CHECK(globalidx < arrlen(ctx->globals));
                size_t offset = ctx->globals[globalidx].offset;
                spidir_value_type_t type = ctx->globals[globalidx].type;
                CHECK(offset != -1);

                // get the value to set
                spidir_value_t value = JIT_POP(type);

                // load it from memory
                spidir_builder_build_store(
                    builder,
                    jit_get_spidir_mem_size(type),
                    value,
                    spidir_builder_build_ptroff(builder, globals_base,
                        spidir_builder_build_iconst(builder, SPIDIR_TYPE_I32, offset))
                );
            } break;

            //----------------------------------------------------------------------------------------------------------
            // Memory instructions
            //----------------------------------------------------------------------------------------------------------

            case 0x28:
            case 0x29:
            case 0x2A:
            case 0x2B:
            case 0x2C:
            case 0x2D:
            case 0x2E:
            case 0x2F:
            case 0x30:
            case 0x31:
            case 0x32:
            case 0x33:
            case 0x34:
            case 0x35: {
                wasm_mem_arg_t mem_arg = {};
                RETHROW(jit_pull_memarg(&code, &mem_arg));
                if (mem_arg.index == 0) {
                    JIT_TRACE("%s %d, %llu", g_wasm_opcode_names[byte], 1 << mem_arg.align, (unsigned long long)mem_arg.offset);
                } else {
                    JIT_TRACE("%s %d, %d, %llu", g_wasm_opcode_names[byte], 1 << mem_arg.align, mem_arg.index, (unsigned long long)mem_arg.offset);
                }

                spidir_value_type_t type;
                spidir_mem_size_t mem_size;
                uint32_t sign_extend = 0;
                uint32_t zero_extend = 0;
                switch (byte) {
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
                    default: ASSERT(!"Invalid instruction");
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
            } break;

            case 0x36:
            case 0x37:
            case 0x38:
            case 0x39:
            case 0x3A:
            case 0x3B:
            case 0x3C:
            case 0x3D:
            case 0x3E: {
                wasm_mem_arg_t mem_arg = {};
                RETHROW(jit_pull_memarg(&code, &mem_arg));
                if (mem_arg.index == 0) {
                    JIT_TRACE("%s %d, %llu", g_wasm_opcode_names[byte], 1 << mem_arg.align, (unsigned long long)mem_arg.offset);
                } else {
                    JIT_TRACE("%s %d, %d, %llu", g_wasm_opcode_names[byte], 1 << mem_arg.align, mem_arg.index, (unsigned long long)mem_arg.offset);
                }

                spidir_value_type_t type;
                spidir_mem_size_t mem_size;
                switch (byte) {
                    case 0x36: type = SPIDIR_TYPE_I32; mem_size = SPIDIR_MEM_SIZE_4; break;
                    case 0x37: type = SPIDIR_TYPE_I64; mem_size = SPIDIR_MEM_SIZE_8;  break;
                    case 0x38: type = SPIDIR_TYPE_F32; mem_size = SPIDIR_MEM_SIZE_4;  break;
                    case 0x39: type = SPIDIR_TYPE_F64; mem_size = SPIDIR_MEM_SIZE_8;  break;
                    case 0x3A: type = SPIDIR_TYPE_I32; mem_size = SPIDIR_MEM_SIZE_1;  break;
                    case 0x3B: type = SPIDIR_TYPE_I32; mem_size = SPIDIR_MEM_SIZE_2;  break;
                    case 0x3C: type = SPIDIR_TYPE_I64; mem_size = SPIDIR_MEM_SIZE_1;  break;
                    case 0x3D: type = SPIDIR_TYPE_I64; mem_size = SPIDIR_MEM_SIZE_2;  break;
                    case 0x3E: type = SPIDIR_TYPE_I64; mem_size = SPIDIR_MEM_SIZE_4;  break;
                    default: ASSERT(!"Invalid instruction");
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
                spidir_builder_build_store(
                    builder,
                    mem_size,
                    value,
                    spidir_builder_build_ptroff(builder, mem_base, offset)
                );
            } break;

            //----------------------------------------------------------------------------------------------------------
            // Numeric instructions
            //----------------------------------------------------------------------------------------------------------

            case 0x41: {
                int32_t value = BUFFER_PULL_I32(&code);
                JIT_TRACE("i32.const %d", value);
                JIT_PUSH(SPIDIR_TYPE_I32, spidir_builder_build_iconst(
                    builder,
                    SPIDIR_TYPE_I32, value
                ));
            } break;

            case 0x42: {
                int64_t value = BUFFER_PULL_I64(&code);
                JIT_TRACE("i64.const %lld", (unsigned long long)value);
                JIT_PUSH(SPIDIR_TYPE_I64, spidir_builder_build_iconst(
                    builder,
                    SPIDIR_TYPE_I64, value
                ));
            } break;

            case 0x43: {
                float value = BUFFER_PULL(float, &code);
                JIT_TRACE("f32.const %f", value);
                JIT_PUSH(SPIDIR_TYPE_F32, spidir_builder_build_fconst32(builder, value));
            } break;

            case 0x44: {
                double value = BUFFER_PULL(double, &code);
                JIT_TRACE("f64.const %lf", value);
                JIT_PUSH(SPIDIR_TYPE_F64, spidir_builder_build_fconst64(builder, value));
            } break;

            // i32.binop
            case 0x6A:
            case 0x6B:
            case 0x6C:
            case 0x6D:
            case 0x6E:
            case 0x6F:
            case 0x70:
            case 0x71:
            case 0x72:
            case 0x73: {
                jit_print_opcode(byte);
                spidir_value_t arg2 = JIT_POP(SPIDIR_TYPE_I32);
                spidir_value_t arg1 = JIT_POP(SPIDIR_TYPE_I32);

                spidir_value_t value = SPIDIR_VALUE_INVALID;
                switch (byte) {
                    case 0x6A: value = spidir_builder_build_iadd(builder, arg1, arg2); break;
                    case 0x6B: value = spidir_builder_build_isub(builder, arg1, arg2); break;
                    case 0x6C: value = spidir_builder_build_imul(builder, arg1, arg2); break;
                    case 0x6D: value = spidir_builder_build_sdiv(builder, arg1, arg2); break;
                    case 0x6E: value = spidir_builder_build_udiv(builder, arg1, arg2); break;
                    case 0x6F: value = spidir_builder_build_srem(builder, arg1, arg2); break;
                    case 0x70: value = spidir_builder_build_urem(builder, arg1, arg2); break;
                    case 0x71: value = spidir_builder_build_and(builder, arg1, arg2); break;
                    case 0x72: value = spidir_builder_build_or(builder, arg1, arg2); break;
                    case 0x73: value = spidir_builder_build_xor(builder, arg1, arg2); break;
                    default: ASSERT(!"Invalid instruction");
                }

                JIT_PUSH(SPIDIR_TYPE_I32, value);
            } break;

            // i64.binop
            case 0x7C:
            case 0x7D:
            case 0x7E:
            case 0x7F:
            case 0x80:
            case 0x81:
            case 0x82:
            case 0x83:
            case 0x84:
            case 0x85: {
                jit_print_opcode(byte);
                spidir_value_t arg2 = JIT_POP(SPIDIR_TYPE_I64);
                spidir_value_t arg1 = JIT_POP(SPIDIR_TYPE_I64);

                spidir_value_t value = SPIDIR_VALUE_INVALID;
                switch (byte) {
                    case 0x7C: value = spidir_builder_build_iadd(builder, arg1, arg2); break;
                    case 0x7D: value = spidir_builder_build_isub(builder, arg1, arg2); break;
                    case 0x7E: value = spidir_builder_build_imul(builder, arg1, arg2); break;
                    case 0x7F: value = spidir_builder_build_sdiv(builder, arg1, arg2); break;
                    case 0x80: value = spidir_builder_build_udiv(builder, arg1, arg2); break;
                    case 0x81: value = spidir_builder_build_srem(builder, arg1, arg2); break;
                    case 0x82: value = spidir_builder_build_urem(builder, arg1, arg2); break;
                    case 0x83: value = spidir_builder_build_and(builder, arg1, arg2); break;
                    case 0x84: value = spidir_builder_build_or(builder, arg1, arg2); break;
                    case 0x85: value = spidir_builder_build_xor(builder, arg1, arg2); break;
                    default: ASSERT(!"Invalid instruction");
                }

                JIT_PUSH(SPIDIR_TYPE_I64, value);
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
    arrfree(locals);

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
