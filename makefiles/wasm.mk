########################################################################################################################
# Spidir
########################################################################################################################

#-----------------------------------------------------------------------------------------------------------------------
# Spidir build setup
#-----------------------------------------------------------------------------------------------------------------------

LIBWASM_DIR := lib/spidir-wasm
LIBWASM_LIBS_PATH := $(LIBWASM_DIR)/build/obj
LIBWASM := $(LIBWASM_LIBS_PATH)/libwasm.a

quiet_cmd_make_libwasm = MAKE    $(LIBWASM)
      cmd_make_libwasm = $(MAKE) -C $(LIBWASM_DIR) build/obj/libwasm.a \
                              OPTIMIZE=$(OPTIMIZE) \
                              CFLAGS="$(COMMON_CFLAGS)" \
                              V=$(V)

PHONY += $(LIBWASM)
$(LIBWASM):
	$(call cmd,make_libwasm)
