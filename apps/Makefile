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
LD := clang
AR := llvm-ar

# Pinned wasi-sdk release — both the sysroot and the compiler-rt
# builtins archive are fetched from this release to keep the build
# reproducible and independent of the host distro's packaging.
WASI_SDK_VERSION := 32
WASI_SDK_URL_BASE := https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-$(WASI_SDK_VERSION)
WASI_SYSROOT_URL := $(WASI_SDK_URL_BASE)/wasi-sysroot-$(WASI_SDK_VERSION).0.tar.gz
WASI_RT_URL      := $(WASI_SDK_URL_BASE)/libclang_rt-$(WASI_SDK_VERSION).0.tar.gz

BUILD := build
ARTIFACTS := artifacts
OBJ := $(BUILD)/obj

WASI_SYSROOT := $(abspath $(ARTIFACTS)/wasi-sysroot)
WASI_SYSROOT_STAMP := $(WASI_SYSROOT)/lib/wasm32-wasi/libc.a

WASI_RT_DIR := $(ARTIFACTS)/wasi-rt
WASI_RT_LIB := $(WASI_RT_DIR)/lib/wasm32-unknown-wasi/libclang_rt.builtins.a

#-----------------------------------------------------------------------------------------------------------------------
# Targets
#-----------------------------------------------------------------------------------------------------------------------

include ../scripts/defs.mk

# Common cflags for all apps
cflags-y += --target=wasm32-wasi
cflags-y += --sysroot=$(WASI_SYSROOT)
cflags-y += -Wall -Werror -std=gnu23
cflags-y += -flto -g
cflags-y += -fvisibility=hidden

ldflags-y += --target=wasm32-wasi
ldflags-y += --sysroot=$(WASI_SYSROOT)
ldflags-y += -resource-dir=$(abspath $(WASI_RT_DIR))
ldflags-y += -flto

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

quiet_cmd_fetch_rt = FETCH   $(WASI_RT_URL)
      cmd_fetch_rt = rm -rf $(WASI_RT_DIR) && \
      				 mkdir -p $(WASI_RT_DIR)/lib && \
                     curl -sSfL $(WASI_RT_URL) \
                       | tar -xz -C $(WASI_RT_DIR)/lib --strip-components=1

$(WASI_RT_LIB): FORCE
	$(call cmd,fetch_rt)

targets += $(WASI_RT_LIB)

quiet_cmd_fetch_sysroot = FETCH   $(WASI_SYSROOT_URL)
      cmd_fetch_sysroot = rm -rf $(WASI_SYSROOT) && \
                          mkdir -p $(WASI_SYSROOT) && \
                          curl -sSfL $(WASI_SYSROOT_URL) \
                            | tar -xz -C $(WASI_SYSROOT) --strip-components=1

$(WASI_SYSROOT_STAMP): FORCE
	$(call cmd,fetch_sysroot)

targets += $(WASI_SYSROOT_STAMP)

include init/Makefile
include ../scripts/build.mk

# Ensure sysroot + compiler-rt builtins are staged before any bin is linked.
$(bin-outputs): $(WASI_RT_LIB) $(WASI_SYSROOT_STAMP)

# Compiles also need the sysroot headers.
ccodeps-y += $(WASI_SYSROOT_STAMP)
