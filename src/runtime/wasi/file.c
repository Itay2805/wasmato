#include "file.h"
#include "alloc/alloc.h"
#include "lib/list.h"
#include "proc/object.h"

wasi_file_t* wasi_file_create(void) {
    wasi_file_t* file = mem_alloc(sizeof(*file));
    if (file == nullptr) {
        return nullptr;
    }

    object_init(&file->object);
    file->object.type = OBJECT_TYPE_WASI_FILE;

    return file;
}

wasi_file_t* wasi_file_from_object(object_t* object) {
    if (object->type == OBJECT_TYPE_WASI_FILE) {
        return containerof(object, wasi_file_t, object);
    }
    return nullptr;
}
