# The rest of the runtime is compiled with support for SSE/AVX, so it
# can do whatever it wants
bins-y += runtime

ldflags-runtime-y += -flto
ldflags-runtime-y += -nostdlib
ldflags-runtime-y += -static

COMMON_CFLAGS := -target x86_64-pc-none-elf
COMMON_CFLAGS += -march=x86-64-v3
COMMON_CFLAGS += -flto -g
COMMON_CFLAGS += -fcf-protection=full -fno-jump-tables
COMMON_CFLAGS += -fno-pie -fno-pic -ffreestanding -fno-builtin -static
COMMON_CFLAGS += -mcmodel=small
COMMON_CFLAGS += -nostdlib -nostdinc
COMMON_CFLAGS += -isystem $(shell $(CC) --print-resource-dir)/include

# common flags
cflags-runtime-y += $(COMMON_CFLAGS)
cflags-runtime-y += -Wall -Werror -std=gnu23
cflags-runtime-y += -Isrc/common
cflags-runtime-y += -fno-omit-frame-pointer -fvisibility=hidden
cflags-runtime-y += -Wno-unused-label
cflags-runtime-y += -mrdpid
cflags-runtime-y += -mfsgsbase
cflags-runtime-y += -Isrc/runtime

cflags-runtime-y += -Ilib/spidir-wasm/include
cflags-runtime-y += -Ilib/spidir-wasm/libs/spidir/c-api/include

cflags-runtime-$(OPTIMIZE) += -O3

cflags-runtime-$(DEBUG) += -fsanitize=undefined
cflags-runtime-$(DEBUG) += -D__DEBUG__
cflags-runtime-$(DEBUG) += -Wno-unused-function -Wno-unused-label -Wno-unused-variable

# link with our rust libs
lddeps-runtime-y += $(RUST_LIBS)
ldflags-runtime-y += -L$(CARGO_LIBS_PATH)
ldlibs-runtime-y += rust_libs

lddeps-runtime-y += $(LIBWASM)
ldflags-runtime-y += -L$(LIBWASM_LIBS_PATH)
ldlibs-runtime-y += wasm

runtime-y += $(shell find src/runtime -name '*.c')
runtime-y += $(shell find src/common -name '*.c')

