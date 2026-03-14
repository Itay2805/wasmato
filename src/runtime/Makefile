# common flags
RUNTIME_CFLAGS += -target x86_64-pc-none-elf
RUNTIME_CFLAGS += -Wall -Werror -std=gnu23
RUNTIME_CFLAGS += -march=x86-64-v3
RUNTIME_CFLAGS += -flto -g
RUNTIME_CFLAGS += -Isrc/common
RUNTIME_CFLAGS += -fno-omit-frame-pointer -fvisibility=hidden
RUNTIME_CFLAGS += -Wno-unused-label

RUNTIME_CFLAGS += -mxsave -mxsaveopt -mwaitpkg
RUNTIME_CFLAGS += -mrdpid
RUNTIME_CFLAGS += -mfsgsbase

RUNTIME_CFLAGS += -fcf-protection=full -fno-jump-tables

RUNTIME_CFLAGS += -fno-pie -fno-pic -ffreestanding -fno-builtin -static
RUNTIME_CFLAGS += -mcmodel=small -mno-red-zone
RUNTIME_CFLAGS += -nostdlib -nostdinc
RUNTIME_CFLAGS += -isystem $(shell $(CC) --print-resource-dir)/include
RUNTIME_CFLAGS += -Isrc/runtime

# eval now
RUNTIME_CFLAGS := $(RUNTIME_CFLAGS)

# The runtime core is basically all the code that needs to run without
# general purpose registers enabled, so the scheduler, interrupt handling
# and so on
libs-y += runtime-core
cflags-runtime-core-y += $(RUNTIME_CFLAGS)
cflags-runtime-core-y += -mgeneral-regs-only

cflags-runtime-core-$(OPTIMIZE) += -O3

cflags-runtime-core-$(DEBUG) += -fsanitize=undefined
cflags-runtime-core-$(DEBUG) += -D__DEBUG__
cflags-runtime-core-$(DEBUG) += -Wno-unused-function -Wno-unused-label -Wno-unused-variable

# The rest of the runtime is compiled with support for SSE/AVX, so it
# can do whatever it wants
bins-y += runtime
ldbuiltlibs-runtime-y += runtime-core

cflags-runtime-y += $(RUNTIME_CFLAGS)

ldflags-runtime-y += -flto
ldflags-runtime-y += -nostdlib
ldflags-runtime-y += -static

cflags-runtime-$(OPTIMIZE) += -O3

cflags-runtime-$(DEBUG) += -fsanitize=undefined
cflags-runtime-$(DEBUG) += -D__DEBUG__
cflags-runtime-$(DEBUG) += -Wno-unused-function -Wno-unused-label -Wno-unused-variable

# link with our rust libs
lddeps-runtime-y += $(RUST_LIBS)
ldflags-runtime-y += -L$(CARGO_LIBS_PATH)
ldlibs-runtime-y += rust_libs

# spidir headers
cflags-runtime-y += -Ilib/spidir/c-api/include

# The core of the runtime, the common
runtime-core-y += $(shell find src/runtime/core -name '*.c')
runtime-core-y += $(shell find src/runtime/core -name '*.S')

# These can be called from interrupts too
runtime-core-y += src/common/debug/ubsan.c
runtime-core-y += src/common/lib/rbtree/rbtree.c
runtime-core-y += src/common/lib/except.c
runtime-core-y += src/common/lib/string_base.c

runtime-y += $(shell find src/runtime -name '*.c')
runtime-y += $(shell find src/common -name '*.c')

# Filter out the core from the runtime
runtime-y := $(filter-out $(runtime-core-y),$(runtime-y))

