RUSTUP_TOOLCHAIN := nightly-2025-10-09
CARGO_TARGET_NAME := x86_64-unknown-linux-none

CRATE_TARGET_DIR := $(BUILD)/cargo-target
ifeq ($(OPTIMIZE),y)
CARGO_LIBS_PATH := $(CRATE_TARGET_DIR)/$(CARGO_TARGET_NAME)/release
else
CARGO_LIBS_PATH := $(CRATE_TARGET_DIR)/$(CARGO_TARGET_NAME)/debug
endif
RUST_LIBS := $(CARGO_LIBS_PATH)/librust_libs.a

RUSTFLAGS += -C force-frame-pointers=yes
RUSTFLAGS += -Z no-jump-tables
RUSTFLAGS += -C linker-plugin-lto=yes
RUSTFLAGS += -C relocation-model=static
RUSTFLAGS += -C target-cpu=x86-64-v3
RUSTFLAGS += -Z cf-protection=full
RUSTFLAGS += -C no-redzone

CARGO_CMD := cargo
CARGO_CMD += +$(RUSTUP_TOOLCHAIN)
CARGO_CMD += rustc
CARGO_CMD += --manifest-path src/rust-libs/Cargo.toml
ifeq ($(OPTIMIZE),y)
CARGO_CMD += --release
endif
CARGO_CMD += --target $(CARGO_TARGET_NAME)
CARGO_CMD += -Zbuild-std=core,alloc
CARGO_CMD += --target-dir $(CRATE_TARGET_DIR)

quiet_cmd_cargo = CARGO   $@
	  cmd_cargo = RUSTFLAGS="$(RUSTFLAGS)" $(CARGO_CMD)

targets += $(RUST_LIBS)

PHONY += rust_libs
rust_libs:
	$(call cmd,cargo)

$(RUST_LIBS): rust_libs
	@:

