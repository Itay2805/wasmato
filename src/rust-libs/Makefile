
RUSTUP_TOOLCHAIN := nightly-2025-10-09
CARGO_TARGET_NAME := x86_64-unknown-linux-none
CARGO_FLAGS := -Zbuild-std=core,alloc

CRATE_TARGET_DIR := $(BUILD)/cargo-target
#CARGO_LIBS_PATH := $(CRATE_TARGET_DIR)/$(CARGO_TARGET_NAME)/release
CARGO_LIBS_PATH := $(CRATE_TARGET_DIR)/$(CARGO_TARGET_NAME)/debug
RUST_LIBS := $(CARGO_LIBS_PATH)/librust_libs.a

CARGO_CMD := cargo
CARGO_CMD += +$(RUSTUP_TOOLCHAIN)
CARGO_CMD += rustc
CARGO_CMD += --manifest-path src/rust-libs/Cargo.toml
#CARGO_CMD += --release
CARGO_CMD += --target $(CARGO_TARGET_NAME)
CARGO_CMD += $(CARGO_FLAGS)
CARGO_CMD += --target-dir $(CRATE_TARGET_DIR)
CARGO_CMD += --
CARGO_CMD += -C force-frame-pointers=yes
CARGO_CMD += -Z no-jump-tables
CARGO_CMD += -C linker-plugin-lto=yes
CARGO_CMD += -C relocation-model=static
CARGO_CMD += -C target-cpu=x86-64-v3
CARGO_CMD += -Z cf-protection=full

quiet_cmd_cargo = CARGO   $@
	  cmd_cargo = $(CARGO_CMD)

targets += $(RUST_LIBS)

PHONY += rust_libs
rust_libs:
	$(call cmd,cargo)

$(RUST_LIBS): rust_libs
	@:

