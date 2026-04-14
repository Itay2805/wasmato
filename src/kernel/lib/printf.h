#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

typedef int (*printf_cb_t)(void* user, const char *buf, size_t size);

__attribute__((format(printf, 4, 0)))
int vcprintf(printf_cb_t cb, void* user, size_t n, const char* fmt, va_list args);

__attribute__((format(printf, 3, 0)))
int vsnprintf(char* buf, size_t size, const char* fmt, va_list args);

__attribute__((format(printf, 3, 4)))
int snprintf(char* buf, size_t size, const char* fmt, ...);
