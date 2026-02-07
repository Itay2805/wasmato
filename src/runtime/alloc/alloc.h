#pragma once

#include "lib/string.h"

void* mem_alloc(size_t len);

void* mem_realloc(void* p, size_t n);

void* mem_alloc_aligned(size_t len, size_t align);

void mem_free(void* p);