#pragma once

#include <stdio.h>
#include <stdarg.h>
#include <uacpi/uacpi.h>
#include <stdlib.h>
#include <stdnoreturn.h>

noreturn static inline void error(const char *format, ...) {
    va_list args;

    fflush(stdout);

    fprintf(stderr, "acpid: unexpected error: ");
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);

    uacpi_state_reset();

    exit(EXIT_FAILURE);
}
