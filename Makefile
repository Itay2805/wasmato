########################################################################################################################
# TomatOS
########################################################################################################################

# Nuke built-in rules and variables.
override MAKEFLAGS += -rR

#-----------------------------------------------------------------------------------------------------------------------
# General Config
#-----------------------------------------------------------------------------------------------------------------------

# Are we compiling as debug or not
DEBUG 			?= y

ifeq ($(DEBUG),1)
OPTIMIZE		?= n
else
OPTIMIZE		?= y
endif

#-----------------------------------------------------------------------------------------------------------------------
# General build flags
#-----------------------------------------------------------------------------------------------------------------------

CC := clang
AR := llvm-ar

BUILD := build
OBJ := $(BUILD)/obj

#-----------------------------------------------------------------------------------------------------------------------
# Targets
#-----------------------------------------------------------------------------------------------------------------------

include scripts/defs.mk

PHONY += all
all:

quiet_cmd_clean = CLEAN   $(BUILD)
      cmd_clean = rm -rf $(BUILD)

PHONY += clean
clean:
	$(call cmd,clean)

include kernel/Makefile
include makefiles/limine.mk
include scripts/build.mk

#-----------------------------------------------------------------------------------------------------------------------
# Qemu helpers
#-----------------------------------------------------------------------------------------------------------------------

# The name of the image we are building
IMAGE_NAME 	:= $(BUILD)/tomato.img

# Build a limine image with both bios and uefi boot options
$(IMAGE_NAME): artifacts/limine.conf $(BUILD)/limine $(BUILD)/kernel
	mkdir -p $(@D)
	rm -f $(IMAGE_NAME)
	dd if=/dev/zero bs=1M count=0 seek=64 of=$(IMAGE_NAME)
	sgdisk $(IMAGE_NAME) -n 1:2048 -t 1:ef00 -m 1
	$(BUILD)/limine bios-install $(IMAGE_NAME)
	mformat -i $(IMAGE_NAME)@@1M
	mmd -i $(IMAGE_NAME)@@1M ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
	mcopy -i $(IMAGE_NAME)@@1M build/kernel ::/boot
	mcopy -i $(IMAGE_NAME)@@1M artifacts/limine.conf ::/boot/limine
	mcopy -i $(IMAGE_NAME)@@1M lib/limine/limine-bios.sys ::/boot/limine
	mcopy -i $(IMAGE_NAME)@@1M lib/limine/BOOTX64.EFI ::/EFI/BOOT
	mcopy -i $(IMAGE_NAME)@@1M lib/limine/BOOTIA32.EFI ::/EFI/BOOT

.PHONY: run
run: $(IMAGE_NAME)
	qemu-system-x86_64 \
		--enable-kvm \
		-cpu host,+invtsc,+tsc-deadline \
		-machine q35 \
		-m 256M \
		-smp 4 \
		-s \
		-hda $(IMAGE_NAME) \
		-debugcon stdio \
		-monitor telnet:127.0.0.1:5555,server,nowait \
		-no-reboot \
	 	-no-shutdown

