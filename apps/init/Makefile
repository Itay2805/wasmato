wasm-y += init

init-y += apps/init/main.c

cflags-init-y += --target=wasm32
cflags-init-y += -Wall -Werror -std=gnu23
cflags-init-y += -flto -g
cflags-init-y += -fvisibility=hidden

cflags-init-y += -nostdlib -nostdinc
cflags-init-y += -isystem $(shell $(CC) --print-resource-dir)/include

# TODO: are there other flags that even make sense?

cflags-init-$(OPTIMIZE) += -O3

# TODO: sanitizers? do they make sense even
cflags-init-$(DEBUG) += -D__DEBUG__
cflags-init-$(DEBUG) += -Wno-unused-function -Wno-unused-label -Wno-unused-variable
