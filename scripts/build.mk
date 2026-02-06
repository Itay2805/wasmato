quiet_cmd_cc = CC      $@
      cmd_cc = $(CC) -MMD -MP $(cflags-y) $(cflags-$(linktarget)-y) $(cflags-$<-y) -c $< -o $@

ldbuiltlibs = $(ldbuiltlibs-$(bin-name)-y:%=$(BUILD)/%.a)
all-ldlibs = $(addprefix -l,$(ldlibs-$(bin-name)-y)) $(ldbuiltlibs)

quiet_cmd_ld = LD      $@
      cmd_ld = $(CC) $(ldflags-y) $(ldflags-$(bin-name)-y) $(objs-$(bin-name)) $(all-ldlibs) -o $@

quiet_cmd_ar = AR      $@
      cmd_ar = rm -f $@; $(AR) rcs $@ $(objs-$(lib-name))

bin-outputs = $(addprefix $(BUILD)/,$(bins-y))
targets += $(bin-outputs)

define each-bin
objs-$(bin-name) := $$($(bin-name)-y:%=$(OBJ)/$(bin-name)/%.o) $$(objs-$(bin-name))

targets += $$(objs-$(bin-name))

$$(objs-$(bin-name)): private linktarget := $(bin-name)
$$(objs-$(bin-name)): $(OBJ)/$(bin-name)/%.o: % FORCE
	$$(call cmd,cc)

$(BUILD)/$(bin-name): private bin-name := $(bin-name)
$(BUILD)/$(bin-name): $$(objs-$(bin-name)) $$(ldbuiltlibs) FORCE
	$$(call cmd,ld)

depfiles += $$(objs-$(bin-name):%.o=%.d)
endef

$(foreach bin-name,$(bins-y),$(eval $(each-bin)))

lib-outputs = $(libs-y:%=$(BUILD)/%.a)
targets += $(lib-outputs)

define each-lib
objs-$(lib-name) := $$($(lib-name)-y:%.c=$(OBJ)/$(lib-name)/%.o) $$(objs-$(lib-name))

targets += $$(objs-$(lib-name))

$$(objs-$(lib-name)): private linktarget := $(lib-name)
$$(objs-$(lib-name)): $(OBJ)/$(lib-name)/%.o: %.c FORCE
	$$(call cmd,cc)

$(BUILD)/$(lib-name).a: private lib-name := $(lib-name)
$(BUILD)/$(lib-name).a: $$(objs-$(lib-name)) FORCE
	$$(call cmd,ar)

depfiles += $$(objs-$(lib-name):%.o=%.d)
endef

$(foreach lib-name,$(libs-y),$(eval $(each-lib)))

all: $(bin-outputs)

PHONY += FORCE
FORCE: ;

existing-targets := $(wildcard $(sort $(targets)))
depfiles += $(existing-targets:%=%.cmd)

.PHONY: $(PHONY)
.DELETE_ON_ERROR:
.SECONDARY:

-include $(depfiles)