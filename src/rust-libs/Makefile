
RUSTUP_TOOLCHAIN := nightly-2025-10-09
CARGO_TARGET_NAME := x86_64-unknown-linux-none
CARGO_FLAGS := -Zbuild-std=core,alloc
RUSTC_FLAGS :=

CRATE_TARGET_DIR := $(BUILD)/cargo-target
CARGO_LIBS_PATH := $(CRATE_TARGET_DIR)/$(CARGO_TARGET_NAME)/release
RUST_LIBS := $(CARGO_LIBS_PATH)/librust_libs.a

CARGO_CMD := cargo
CARGO_CMD += +$(RUSTUP_TOOLCHAIN)
CARGO_CMD += rustc
CARGO_CMD += --manifest-path src/rust-libs/Cargo.toml
CARGO_CMD += --release
CARGO_CMD += --target $(CARGO_TARGET_NAME)
CARGO_CMD += $(CARGO_FLAGS)
CARGO_CMD += --target-dir $(CRATE_TARGET_DIR)
CARGO_CMD += --
CARGO_CMD += -C force-frame-pointers=yes
CARGO_CMD += $(RUSTC_FLAGS)

quiet_cmd_cargo = CARGO $@
	  cmd_cargo = $(CARGO_CMD)

targets += $(RUST_LIBS)
$(RUST_LIBS): FORCE
	$(call cmd,cargo)


