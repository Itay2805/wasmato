#include "buffer.h"

err_t buffer_pull_u32(buffer_t* buffer, uint32_t* value) {
    err_t err = NO_ERROR;

    uint32_t result = 0;
    uint32_t shift = 0;
    uint8_t byte = 0;
    do {
        CHECK(shift <= 28);
        byte = BUFFER_PULL(uint8_t, buffer);
        result |= (byte & 0x7f) << shift;
        shift += 7;
    } while ((byte & 0x80) != 0);

    *value = result;

cleanup:
    return err;
}

err_t buffer_pull_u64(buffer_t* buffer, uint64_t* value) {
    err_t err = NO_ERROR;

    uint64_t result = 0;
    uint32_t shift = 0;
    uint8_t byte = 0;
    do {
        CHECK(shift <= 56);
        byte = BUFFER_PULL(uint8_t, buffer);
        result |= ((uint64_t)byte & 0x7f) << shift;
        shift += 7;
    } while ((byte & 0x80) != 0);

    *value = result;

cleanup:
    return err;
}

err_t buffer_pull_i32(buffer_t* buffer, int32_t* out) {
    err_t err = NO_ERROR;

    int32_t result = 0;
    uint32_t shift = 0;

    uint8_t byte;
    do {
        byte = BUFFER_PULL(uint8_t, buffer);
        result |= ((byte & 0x7F) << shift);
        shift += 7;
    } while ((byte & 0x80) != 0);

    if ((shift < 32) && (byte & 0x40)) {
        result |= ~0 << shift;
    }

    *out = result;

    cleanup:
        return err;
}

err_t buffer_pull_i64(buffer_t* buffer, int64_t* out) {
    err_t err = NO_ERROR;

    int64_t result = 0;
    uint32_t shift = 0;

    uint8_t byte;
    do {
        byte = BUFFER_PULL(uint8_t, buffer);
        result |= (((int64_t)byte & 0x7F) << shift);
        shift += 7;
    } while ((byte & 0x80) != 0);

    if ((shift < 64) && (byte & 0x40)) {
        result |= (~0ll << shift);
    }

    *out = result;

cleanup:
    return err;
}

err_t buffer_pull_name(buffer_t* buffer, buffer_t* name) {
    err_t err = NO_ERROR;

    name->len = BUFFER_PULL_U32(buffer);
    name->data = buffer_pull(buffer, name->len);
    CHECK(name->data != nullptr);

    // TODO: verify utf8

cleanup:
    return err;
}

err_t buffer_pull_val_type(buffer_t* buffer, wasm_value_type_t* valtype) {
    err_t err = NO_ERROR;

    uint8_t byte = BUFFER_PULL(uint8_t, buffer);
    switch (byte) {
        case 0x7C: *valtype = WASM_VALUE_TYPE_F64; break;
        case 0x7D: *valtype = WASM_VALUE_TYPE_F32; break;
        case 0x7E: *valtype = WASM_VALUE_TYPE_I64; break;
        case 0x7F: *valtype = WASM_VALUE_TYPE_I32; break;
        default: CHECK_FAIL("%x", byte);
    }

    cleanup:
        return err;
}
