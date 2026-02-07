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

ifeq ($(DEBUG),y)
OPTIMIZE		?= n
else
OPTIMIZE		?= y
endif

#-----------------------------------------------------------------------------------------------------------------------
# General build flags
#-----------------------------------------------------------------------------------------------------------------------

CC := clang
AS := clang
AR := llvm-ar

CLANG_RESOURCE_DIR := $(shell $(CC) --print-resource-dir)

BUILD := build
OBJ := $(BUILD)/obj

#-----------------------------------------------------------------------------------------------------------------------
# Targets
#-----------------------------------------------------------------------------------------------------------------------

include scripts/defs.mk

ldflags-y += -fuse-ld=lld

PHONY += all
all:

quiet_cmd_clean = CLEAN   $(BUILD)
      cmd_clean = rm -rf $(BUILD)

PHONY += clean
clean:
	$(call cmd,clean)

include src/rust-libs/Makefile
include src/runtime/Makefile
include src/kernel/Makefile
include makefiles/limine.mk
include makefiles/test.mk
include scripts/build.mk
