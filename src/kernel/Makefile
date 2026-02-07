
bins-y += kernel

cflags-kernel-y += -target x86_64-pc-none-elf
cflags-kernel-y += -Wall -Werror -std=gnu23
cflags-kernel-y += -march=x86-64-v3 -mxsave -mxsaveopt
cflags-kernel-y += -flto -g
cflags-kernel-y += -fno-omit-frame-pointer -fvisibility=hidden
cflags-kernel-y += -Isrc/common
cflags-kernel-y += -Wno-unused-label

cflags-kernel-y += -mgeneral-regs-only
cflags-kernel-y += -fno-pie -fno-pic -ffreestanding -fno-builtin -static
cflags-kernel-y += -mcmodel=kernel -mno-red-zone
cflags-kernel-y += -nostdlib -nostdinc
cflags-kernel-y += -isystem $(shell $(CC) --print-resource-dir)/include
cflags-kernel-y += -Isrc/kernel
cflags-kernel-y += -Ilib/limine-protocol/include

# we need the runtime for the kernel to work
ccodeps-kernel-y += $(BUILD)/runtime

# ensure we link with lto and all the fun
ldflags-kernel-y += -flto
ldflags-kernel-y += -nostdlib
ldflags-kernel-y += -static

# linker script
lddeps-kernel-y += src/kernel/linker.ld
ldflags-kernel-y += -Tsrc/kernel/linker.ld

# don't include floating point support in the printf for the kernel
cflags-kernel-y += -DSTB_SPRINTF_NOFLOAT

#
cflags-kernel-$(OPTIMIZE) += -O3
ldflags-kernel-$(OPTIMIZE) += -O3

cflags-kernel-$(DEBUG) += -fsanitize=undefined
cflags-kernel-$(DEBUG) += -D__DEBUG__
cflags-kernel-$(DEBUG) += -Wno-unused-function -Wno-unused-label -Wno-unused-variable

kernel-y += $(shell find src/kernel -name '*.c')
kernel-y += $(shell find src/kernel -name '*.S')

kernel-y += $(shell find src/common -name '*.c')
