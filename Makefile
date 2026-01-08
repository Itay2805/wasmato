########################################################################################################################
# TomatOS
########################################################################################################################

# Nuke built-in rules and variables.
override MAKEFLAGS += -rR

.SECONDARY:
.DELETE_ON_ERROR:

KERNEL			:= tomatos

#-----------------------------------------------------------------------------------------------------------------------
# General Config
#-----------------------------------------------------------------------------------------------------------------------

# Are we compiling as debug or not
DEBUG 			?= 0

ifeq ($(DEBUG),1)
OPTIMIZE		?= 0
else
OPTIMIZE		?= 1
endif

#-----------------------------------------------------------------------------------------------------------------------
# Directories
#-----------------------------------------------------------------------------------------------------------------------

BUILD_DIR		:= build
BIN_DIR			:= $(BUILD_DIR)/bin
OBJS_DIR		:= $(BUILD_DIR)/obj

#-----------------------------------------------------------------------------------------------------------------------
# Flags
#-----------------------------------------------------------------------------------------------------------------------

#
# Toolchain
#
CC				:= clang
AR				:= llvm-ar
LD				:= ld.lld

#
# Common compilation flags, also passed to the libraries
#
CFLAGS			:= -target x86_64-pc-none-elf
CFLAGS			+= -Wall -Werror -std=gnu11
CFLAGS			+= -mgeneral-regs-only
CFLAGS			+= -march=x86-64-v3 -mxsave -mxsaveopt
CFLAGS			+= -fno-pie -fno-pic -ffreestanding -fno-builtin -static
CFLAGS			+= -mcmodel=kernel -mno-red-zone
CFLAGS			+= -nostdlib -nostdinc
CFLAGS			+= -flto -g
CFLAGS			+= -fno-omit-frame-pointer -fvisibility=hidden
CFLAGS			+= -isystem $(shell $(CC) --print-resource-dir)/include
CFLAGS			+= -Ikernel
CFLAGS			+= -Ilib/limine-protocol/include
CFLAGS			+= -Wno-unused-label

#
# Linker flags
#
LDFLAGS			:= -Tkernel/linker.ld
LDFLAGS			+= --nostdlib
LDFLAGS			+= --static
LDFLAGS			+= --no-pie


# Optimization flags
ifeq ($(OPTIMIZE),1)
CFLAGS			+= -O3
LDFLAGS			+= -O2
LDFLAGS			+= --icf=all
else
CFLAGS			+= -O0
LDFLAGS			+= -O0
LDFLAGS			+= --icf=none
endif

# Debug flags
ifeq ($(DEBUG),1)
CFLAGS			+= -fsanitize=undefined
CFLAGS			+= -D__DEBUG__
CFLAGS			+= -Wno-unused-function -Wno-unused-label -Wno-unused-variable
endif

#-----------------------------------------------------------------------------------------------------------------------
# Stb printf
#-----------------------------------------------------------------------------------------------------------------------

CFLAGS 			+= -DSTB_SPRINTF_NOFLOAT

#-----------------------------------------------------------------------------------------------------------------------
# Sources
#-----------------------------------------------------------------------------------------------------------------------

# Get list of source files
SRCS 		:= $(shell find kernel -name '*.c')
SRCS 		+= $(shell find kernel -name '*.S')

# The objects/deps
OBJS 		:= $(SRCS:%=$(OBJS_DIR)/%.o)
DEPS 		:= $(OBJS:%.o=%.d)

# Default target.
.PHONY: all
all: $(BIN_DIR)/$(KERNEL).elf

# Get the header deps
-include $(DEPS)

# Link rules for the final kernel executable.
$(BIN_DIR)/$(KERNEL).elf: kernel/linker.ld $(OBJS)
	@echo LD $@
	@mkdir -p $(@D)
	@$(LD) $(OBJS) $(LDFLAGS) -o $@

$(OBJS_DIR)/%.c.o: %.c
	@echo CC $@
	@mkdir -p $(@D)
	@$(CC) -MD -MP $(CFLAGS) -c $< -o $@

$(OBJS_DIR)/%.S.o: %.S
	@echo CC $@
	@mkdir -p $(@D)
	@$(CC) -MD -MP $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

#-----------------------------------------------------------------------------------------------------------------------
# All the binaries
#-----------------------------------------------------------------------------------------------------------------------

# TODO: modules

#-----------------------------------------------------------------------------------------------------------------------
# Quick test
#-----------------------------------------------------------------------------------------------------------------------

# The name of the image we are building
IMAGE_NAME 	:= $(BIN_DIR)/$(KERNEL)

.PHONY: FORCE
FORCE:

$(OBJS_DIR)/limine.stamp: $(shell find lib/limine)
	@mkdir -p $(@D)
	@cp -rpT lib/limine $(OBJS_DIR)/limine
	@touch $@

$(OBJS_DIR)/limine/limine: FORCE | $(OBJS_DIR)/limine.stamp
	@$(MAKE) -C $(OBJS_DIR)/limine

# Build a limine image with both bios and uefi boot options
$(IMAGE_NAME).hdd: artifacts/limine.conf $(OBJS_DIR)/limine/limine $(BIN_DIR)/$(KERNEL).elf $(WASM)
	mkdir -p $(@D)
	rm -f $(IMAGE_NAME).hdd
	dd if=/dev/zero bs=1M count=0 seek=64 of=$(IMAGE_NAME).hdd
	sgdisk $(IMAGE_NAME).hdd -n 1:2048 -t 1:ef00 -m 1
	$(OBJS_DIR)/limine/limine bios-install $(IMAGE_NAME).hdd
	mformat -i $(IMAGE_NAME).hdd@@1M
	mmd -i $(IMAGE_NAME).hdd@@1M ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
	mcopy -i $(IMAGE_NAME).hdd@@1M $(BIN_DIR)/$(KERNEL).elf ::/boot
	mcopy -i $(IMAGE_NAME).hdd@@1M $(WASM) ::/
	mcopy -i $(IMAGE_NAME).hdd@@1M artifacts/limine.conf ::/boot/limine
	mcopy -i $(IMAGE_NAME).hdd@@1M lib/limine/limine-bios.sys ::/boot/limine
	mcopy -i $(IMAGE_NAME).hdd@@1M lib/limine/BOOTX64.EFI ::/EFI/BOOT
	mcopy -i $(IMAGE_NAME).hdd@@1M lib/limine/BOOTIA32.EFI ::/EFI/BOOT

.PHONY: run
run: $(IMAGE_NAME).hdd
	qemu-system-x86_64 \
		--enable-kvm \
		-cpu host,+invtsc,+tsc-deadline \
		-machine q35 \
		-m 256M \
		-smp 1 \
		-s \
		-hda $(IMAGE_NAME).hdd \
		-debugcon stdio \
		-monitor tcp:127.0.0.1:5555,server,nowait \
		-no-reboot \
	 	-no-shutdown

