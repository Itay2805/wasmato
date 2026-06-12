#pragma once

#include <stdio.h>

#define TRACE(fmt, ...) printf("[*] " fmt "\n", ##__VA_ARGS__)
#define WARN(fmt, ...) printf("[!] " fmt "\n", ##__VA_ARGS__)
#define ERROR(fmt, ...) printf("[-] " fmt "\n", ##__VA_ARGS__)
