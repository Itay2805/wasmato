#pragma once

#include <stdarg.h>

#include "lib/defs.h"

// log levels
#define DEBUG(fmt, ...)     debug_print("[?] " fmt "\n", ##__VA_ARGS__)
#define TRACE(fmt, ...)     debug_print("[*] " fmt "\n", ##__VA_ARGS__)
#define WARN(fmt, ...)      debug_print("[!] " fmt "\n", ##__VA_ARGS__)
#define ERROR(fmt, ...)     debug_print("[-] " fmt "\n", ##__VA_ARGS__)

#define WARN_ON(expr, fmt, ...) \
    do { \
        if (expr) { \
            WARN(fmt, ##__VA_ARGS__); \
        } \
    } while (0)

/**
 * Early logging initialization
 */
void init_early_logging(void);

/**
 * Print a string to the debug log
 */
void debug_print(const char* fmt, ...) __attribute__((format(printf, (1), (2))));
