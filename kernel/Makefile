
bins-y += kernel

cflags-kernel-y += -mgeneral-regs-only
cflags-kernel-y += -fno-pie -fno-pic -ffreestanding -fno-builtin -static
cflags-kernel-y += -mcmodel=kernel -mno-red-zone
cflags-kernel-y += -nostdlib -nostdinc
cflags-kernel-y += -isystem $(shell $(CC) --print-resource-dir)/include
cflags-kernel-y += -Ikernel
cflags-kernel-y += -Ilib/limine-protocol/include

cflags-kernel-y += -Wno-gnu-variable-sized-type-not-at-end

ldflags-kernel-y += -Tkernel/linker.ld
ldflags-kernel-y += -nostdlib
ldflags-kernel-y += -static

cflags-kernel-y += -DSTB_SPRINTF_NOFLOAT

kernel-y += $(shell find kernel -name '*.c')
kernel-y += $(shell find kernel -name '*.S')
