
bins-y += runtime

cflags-runtime-y += -target x86_64-pc-none-elf
cflags-runtime-y += -Wall -Werror -std=gnu23
cflags-runtime-y += -march=x86-64-v3
cflags-runtime-y += -flto -g
cflags-runtime-y += -Isrc/common
cflags-runtime-y += -fno-omit-frame-pointer -fvisibility=hidden
cflags-runtime-y += -Wno-unused-label

cflags-runtime-y += -fno-pie -fno-pic -ffreestanding -fno-builtin -static
cflags-runtime-y += -mcmodel=small
cflags-runtime-y += -nostdlib -nostdinc
cflags-runtime-y += -isystem $(shell $(CC) --print-resource-dir)/include
cflags-runtime-y += -Isrc/runtime

ldflags-runtime-y += -flto
ldflags-runtime-y += -nostdlib
ldflags-runtime-y += -static

cflags-runtime-$(OPTIMIZE) += -O3

cflags-runtime-$(DEBUG) += -fsanitize=undefined
cflags-runtime-$(DEBUG) += -D__DEBUG__
cflags-runtime-$(DEBUG) += -Wno-unused-function -Wno-unused-label -Wno-unused-variable

runtime-y += $(shell find src/runtime -name '*.c')
runtime-y += $(shell find src/common -name '*.c')
