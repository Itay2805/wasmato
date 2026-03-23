########################################################################################################################
# TomatOS usermode apps
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
LD := wasm-ld
AR := llvm-ar

CLANG_RESOURCE_DIR := $(shell $(CC) --print-resource-dir)

BUILD := build
OBJ := $(BUILD)/obj

#-----------------------------------------------------------------------------------------------------------------------
# Targets
#-----------------------------------------------------------------------------------------------------------------------

include ../scripts/defs.mk

# Common cflags for all apps
cflags-y += --target=wasm32
cflags-y += -Wall -Werror -std=gnu23
cflags-y += -flto -g
cflags-y += -fvisibility=hidden
cflags-y += -nostdlib -nostdinc
cflags-y += -isystem $(shell $(CC) --print-resource-dir)/include

cflags-$(OPTIMIZE) += -O3

cflags-$(DEBUG) += -D__DEBUG__
cflags-$(DEBUG) += -Wno-unused-function -Wno-unused-label -Wno-unused-variable

PHONY += all
all:

quiet_cmd_clean = CLEAN   $(BUILD)
      cmd_clean = rm -rf $(BUILD)

PHONY += clean
clean:
	$(call cmd,clean)

include init/Makefile
include ../scripts/build.mk
