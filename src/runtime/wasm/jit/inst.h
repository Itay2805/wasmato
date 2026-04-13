#pragma once

#include <spidir/module.h>

#include "jit_internal.h"
#include "wasm/buffer.h"

typedef struct jit_value {
    spidir_value_t value;
    spidir_value_type_t type;
} jit_value_t;

typedef struct jit_label {
    // the block of this label
    spidir_block_t block;

    // the current stack of the label
    jit_value_t* stack;

    // the phis of all the locals in this block
    spidir_phi_t* locals_phis;

    // the values of the locals
    spidir_value_t* locals_values;

    // this is a loop block, meaning we jump back
    // into the starting block instead of jumping
    // into the end
    bool loop;

    // did we terminate the block yet
    bool terminated;
} jit_label_t;

typedef struct jit_instruction_ctx {
    // the types of all the locals
    spidir_value_type_t* locals_types;

    // handle returning values
    spidir_value_type_t ret_type;

    // the labels stack
    jit_label_t* labels;
} jit_instruction_ctx_t;

typedef err_t (*jit_instruction_t)(spidir_builder_handle_t builder, buffer_t* code, jit_context_t* ctx, jit_instruction_ctx_t* inst, jit_label_t* label);

void jit_free_label(jit_label_t* label);

extern const jit_instruction_t g_wasm_inst_jit_callbacks[0x100];
