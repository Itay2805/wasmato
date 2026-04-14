#include "module.h"
#include "jit/jit.h"

#include "buffer.h"

#include "spec.h"
#include "lib/stb_ds.h"

static void wasm_type_free(wasm_type_t* type) {
    switch (type->kind) {
        case WASM_TYPE_KIND_FUNC: {
            arrfree(type->func.arg_types);
            arrfree(type->func.result_types);
        } break;
        default: ASSERT(false);
    }
}

void wasm_module_free(wasm_module_t* module) {
    for (int i = 0; i < arrlen(module->types); i++) {
        wasm_type_free(&module->types[i]);
    }

    for (int i = 0; i < arrlen(module->imports); i++) {
        arrfree(module->imports[i].item_name);
        arrfree(module->imports[i].module_name);
    }

    for (int i = 0; i < arrlen(module->code); i++) {
        mem_free(module->code[i].code);
    }

    arrfree(module->types);
    arrfree(module->imports);
    arrfree(module->functions);
    arrfree(module->globals);
    shfree(module->exports);
    arrfree(module->code);
}

static err_t module_pull_magic_version(buffer_t* buffer) {
    err_t err = NO_ERROR;

    char* magic_version = buffer_pull(buffer, 8);
    CHECK(magic_version != nullptr);
    CHECK(magic_version[0] == 0x00);
    CHECK(magic_version[1] == 0x61);
    CHECK(magic_version[2] == 0x73);
    CHECK(magic_version[3] == 0x6D);
    CHECK(magic_version[4] == 0x01);
    CHECK(magic_version[5] == 0x00);
    CHECK(magic_version[6] == 0x00);
    CHECK(magic_version[7] == 0x00);

cleanup:
    return err;
}
static err_t wasm_pull_section(buffer_t* buffer, wasm_section_t* section) {
    err_t err = NO_ERROR;

    section->id = BUFFER_PULL(wasm_section_id_t, buffer);
    section->contents.len = BUFFER_PULL_U32(buffer);
    section->contents.data = buffer_pull(buffer, section->contents.len);
    CHECK(section->contents.data != nullptr);

cleanup:
    return err;
}

static const wasm_section_id_t m_expected_section_order[] = {
    WASM_SECTION_TYPE,
    WASM_SECTION_IMPORT,
    WASM_SECTION_FUNCTION,
    WASM_SECTION_TABLE,
    WASM_SECTION_MEMORY,
    WASM_SECTION_TAG,
    WASM_SECTION_GLOBAL,
    WASM_SECTION_EXPORT,
    WASM_SECTION_START,
    WASM_SECTION_ELEMENT,
    WASM_SECTION_DATA_COUNT,
    WASM_SECTION_CODE,
    WASM_SECTION_DATA
};

static int find_section_index(wasm_section_id_t id, int index) {
    for (int i = index; i < ARRAY_LENGTH(m_expected_section_order); i++) {
        if (m_expected_section_order[i] == id) {
            return i;
        }
    }
    return -1;
}

static err_t wasm_pull_result_type(buffer_t* buffer, wasm_value_type_t** out_types) {
    err_t err = NO_ERROR;

    uint32_t count = BUFFER_PULL_U32(buffer);
    wasm_value_type_t* types = nullptr;
    arrsetcap(types, count);

    for (int i = 0; i < count; i++) {
        wasm_value_type_t type;
        RETHROW(buffer_pull_val_type(buffer, &type));
        arrpush(types, type);
    }

    *out_types = types;

cleanup:
    if (IS_ERROR(err)) {
        arrfree(types);
    }

    return err;
}

static err_t wasm_parse_type_section(wasm_module_t* module, buffer_t* buffer) {
    err_t err = NO_ERROR;

    uint32_t type_count = BUFFER_PULL_U32(buffer);
    arrsetcap(module->types, type_count);

    for (int i = 0; i < type_count; i++) {
        uint8_t type = BUFFER_PULL(uint8_t, buffer);
        wasm_type_t wasm_type = {};

        switch (type) {
            case 0x60: {
                wasm_type.kind = WASM_TYPE_KIND_FUNC;
                RETHROW(wasm_pull_result_type(buffer, &wasm_type.func.arg_types));
                RETHROW(wasm_pull_result_type(buffer, &wasm_type.func.result_types));
            } break;

            default: {
                CHECK_FAIL("Unknown type %x", type);
            } break;
        }

        arrpush(module->types, wasm_type);
    }

    CHECK(buffer->len == 0);

cleanup:
    return err;
}
static err_t wasm_parse_import_section(wasm_module_t* module, buffer_t* buffer) {
    err_t err = NO_ERROR;
    char* module_name = nullptr;
    char* item_name = nullptr;

    uint32_t count = BUFFER_PULL_U32(buffer);
    arrsetcap(module->imports, count);

    for (int i = 0; i < count; i++) {
        // pull the module name
        buffer_t module_name_buf = {};
        RETHROW(buffer_pull_name(buffer, &module_name_buf));
        CHECK(module_name_buf.len > 0);
        arrsetlen(module_name, module_name_buf.len + 1);
        memcpy(module_name, module_name_buf.data, module_name_buf.len);
        module_name[module_name_buf.len] = '\0';

        // pull the item name
        buffer_t item_name_buf = {};
        RETHROW(buffer_pull_name(buffer, &item_name_buf));
        CHECK(item_name_buf.len > 0);
        arrsetlen(item_name, item_name_buf.len + 1);
        memcpy(item_name, item_name_buf.data, item_name_buf.len);
        item_name[item_name_buf.len] = '\0';

        // get the type and index
        uint8_t byte = BUFFER_PULL(uint8_t, buffer);

        // get the index
        wasm_extern_type_t kind;
        uint32_t index = BUFFER_PULL_U32(buffer);
        switch (byte) {
            case 0x00: {
                CHECK(index < arrlen(module->types));
                kind = WASM_EXTERN_FUNC;
            } break;

            default: {
                CHECK_FAIL("Unknown export type %x (%s, %s)", byte, module_name, item_name);
            } break;
        }

        // and now insert it into the hashmap
        wasm_import_t import = {
            .item_name = item_name,
            .module_name = module_name,
            .kind = kind,
            .index = index,
        };
        arrpush(module->imports, import);

        module_name = nullptr;
        item_name = nullptr;
    }

    CHECK(buffer->len == 0);

cleanup:
    arrfree(module_name);
    arrfree(item_name);

    return err;
}

static err_t wasm_parse_function_section(wasm_module_t* module, buffer_t* buffer) {
    err_t err = NO_ERROR;

    uint32_t count = BUFFER_PULL_U32(buffer);
    arrsetcap(module->functions, count);

    for (int i = 0; i < count; i++) {
        uint32_t typeidx = BUFFER_PULL_U32(buffer);
        CHECK(typeidx < arrlen(module->types));
        CHECK(module->types[typeidx].kind == WASM_TYPE_KIND_FUNC);
        arrpush(module->functions, typeidx);
    }

    CHECK(buffer->len == 0);

cleanup:
    return err;
}

static err_t wasm_parse_memory_section(wasm_module_t* module, buffer_t* buffer) {
    err_t err = NO_ERROR;

    uint32_t count = BUFFER_PULL_U32(buffer);
    CHECK(count == 1, "Multi-memory is not supported");

    // TODO: multi-memory support
    // TODO: support for 64bit addresses

    uint8_t limit_type = BUFFER_PULL(uint8_t, buffer);
    if (limit_type == 0x00) {
        module->memory_min = BUFFER_PULL_U64(buffer);
        module->memory_max = (UINT32_MAX / WASM_PAGE_SIZE) - 1;
    } else if (limit_type == 0x01) {
        module->memory_min = BUFFER_PULL_U64(buffer);
        module->memory_max = BUFFER_PULL_U64(buffer);
    } else {
        CHECK_FAIL("Invalid limit %02x", limit_type);
    }

    // ensure the min is less or equals to the max
    CHECK(module->memory_min <= module->memory_max);

    // turn into bytes instead of pages
    CHECK(!__builtin_mul_overflow(module->memory_min, WASM_PAGE_SIZE, &module->memory_min));
    CHECK(!__builtin_mul_overflow(module->memory_max, WASM_PAGE_SIZE, &module->memory_max));

    // ensure both stay within 4GB
    CHECK(module->memory_min < SIZE_4GB);
    CHECK(module->memory_max < SIZE_4GB);

    CHECK(buffer->len == 0);

cleanup:
    return err;
}

static err_t wasm_parse_constant_expr(buffer_t* buffer, wasm_value_t* value) {
    err_t err = NO_ERROR;

    // get the expression
    uint8_t byte = BUFFER_PULL(uint8_t, buffer);
    switch (byte) {
        case 0x41: {
            value->kind = WASM_VALUE_TYPE_I32;
            value->value.i32 = BUFFER_PULL_I32(buffer);
        } break;

        case 0x42: {
            value->kind = WASM_VALUE_TYPE_I64;
            value->value.i64 = BUFFER_PULL_I64(buffer);
        } break;

        case 0x43: {
            value->kind = WASM_VALUE_TYPE_F32;
            value->value.f32 = BUFFER_PULL(float, buffer);
        } break;

        case 0x44: {
            value->kind = WASM_VALUE_TYPE_F64;
            value->value.f64 = BUFFER_PULL(double, buffer);
        } break;

        default:
            CHECK_FAIL("%x", byte);
    }

    // ensure we end up with expression end
    CHECK(BUFFER_PULL(uint8_t, buffer) == 0x0B);

cleanup:
    return err;
}

err_t wasm_parse_global_section(wasm_module_t* module, buffer_t* buffer) {
    err_t err = NO_ERROR;

    uint32_t count = BUFFER_PULL_U32(buffer);
    arrsetcap(module->globals, count);

    for (int i = 0; i < count; i++) {
        wasm_value_type_t type;
        RETHROW(buffer_pull_val_type(buffer, &type));
        uint8_t mut = BUFFER_PULL(uint8_t, buffer);
        CHECK(mut == 0x00 || mut == 0x01);

        wasm_global_t global = {
            .mutable = mut == 0x01
        };

        // parse the expression and ensure we get the correct
        // type at the end of it
        RETHROW(wasm_parse_constant_expr(buffer, &global.value));
        CHECK(global.value.kind == type);

        // append it
        arrpush(module->globals, global);
    }

    CHECK(buffer->len == 0);

cleanup:
    return err;
}

err_t wasm_parse_export_section(wasm_module_t* module, buffer_t* buffer) {
    err_t err = NO_ERROR;
    char* name = nullptr;

    uint32_t count = BUFFER_PULL_U32(buffer);
    sh_new_arena(module->exports);

    for (int i = 0; i < count; i++) {
        // create a null terminated copy of the name
        buffer_t name_buf = {};
        RETHROW(buffer_pull_name(buffer, &name_buf));
        CHECK(name_buf.len > 0);
        arrsetlen(name, name_buf.len + 1);
        memcpy(name, name_buf.data, name_buf.len);
        name[name_buf.len] = '\0';

        // get the type and index
        uint8_t byte = BUFFER_PULL(uint8_t, buffer);

        // get the index
        wasm_export_type_t kind;
        uint32_t index = BUFFER_PULL_U32(buffer);
        switch (byte) {
            case 0x00: CHECK(index < arrlen(module->functions)); kind = WASM_EXPORT_FUNC; break;
            case 0x02: CHECK(index == 0); kind = WASM_EXPORT_MEMORY; break;
            case 0x03: CHECK(index < arrlen(module->globals)); kind = WASM_EXPORT_GLOBAL; break;
            default: CHECK_FAIL("Unknown export type %x (%s)", byte, name);
        }

        // and now insert it into the hashmap
        wasm_export_t export = {
            .kind = kind,
            .index = index,
            .key = name
        };
        shputs(module->exports, export);
    }

    CHECK(buffer->len == 0);

cleanup:
    arrfree(name);

    return err;
}

err_t wasm_parse_code_section(wasm_module_t* module, buffer_t* buffer) {
    err_t err = NO_ERROR;

    uint32_t count = BUFFER_PULL_U32(buffer);
    CHECK(count == arrlen(module->functions));

    for (int i = 0; i < count; i++) {
        wasm_code_t* code = arraddnptr(module->code, 1);
        *code = (wasm_code_t){};

        code->length = BUFFER_PULL_U32(buffer);
        void* data = buffer_pull(buffer, code->length);
        CHECK(data != nullptr);

        code->code = mem_alloc(code->length);
        memcpy(code->code, data, code->length);
    }

    CHECK(buffer->len == 0);

cleanup:
    return err;
}

err_t wasm_load_module(wasm_module_t* module, void* data, size_t size) {
    err_t err = NO_ERROR;

    TRACE("wasm: loading module");
    buffer_t buffer = init_buffer(data, size);
    RETHROW(module_pull_magic_version(&buffer));

    int current_index = 0;
    while (buffer.len != 0) {
        wasm_section_t section = {};
        RETHROW(wasm_pull_section(&buffer, &section));

        // check ordering
        if (section.id != 0) {
            int index = find_section_index(section.id, current_index);
            CHECK(index >= current_index);
            current_index = index + 1;
        }

        // actually check the section
        buffer_t* contents = &section.contents;
        switch (section.id) {
            case WASM_SECTION_CUSTOM: {
                buffer_t name = {};
                RETHROW(buffer_pull_name(contents, &name));

                // just ignore the custom sections for now
            } break;

            case WASM_SECTION_TYPE: RETHROW(wasm_parse_type_section(module, contents)); break;
            case WASM_SECTION_IMPORT: RETHROW(wasm_parse_import_section(module, contents)); break;
            case WASM_SECTION_FUNCTION: RETHROW(wasm_parse_function_section(module, contents)); break;
            case WASM_SECTION_MEMORY: RETHROW(wasm_parse_memory_section(module, contents)); break;
            case WASM_SECTION_GLOBAL: RETHROW(wasm_parse_global_section(module, contents)); break;
            case WASM_SECTION_EXPORT: RETHROW(wasm_parse_export_section(module, contents)); break;
            case WASM_SECTION_CODE: RETHROW(wasm_parse_code_section(module, contents)); break;

            default: {
                TRACE("wasm: ignoring section %d", section.id);
                // just ignore the other sections...
            } break;
        }
    }

cleanup:
    if (IS_ERROR(err)) {
        wasm_module_free(module);
    }

    return err;
}

wasm_export_t* wasm_find_export(wasm_module_t* module, const char* name) {
    return shgetp_null(module->exports, name);
}
