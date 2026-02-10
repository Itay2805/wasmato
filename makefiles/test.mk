IMAGE_NAME 	:= $(BUILD)/tomato.img

quiet_cmd_mkimage = MKIMAGE $@
      cmd_mkimage = \
          rm -f $@; \
          dd if=/dev/zero bs=1M count=0 seek=64 of=$@; \
          sgdisk $@ -n 1:2048 -t 1:ef00 -m 1; \
          $(BUILD)/limine bios-install $@; \
          mformat -i $@@@1M; \
          mmd -i $@@@1M ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine; \
          mcopy -i $@@@1M build/kernel ::/boot; \
          mcopy -i $@@@1M artifacts/limine.conf ::/boot/limine; \
          mcopy -i $@@@1M lib/limine/limine-bios.sys ::/boot/limine; \
          mcopy -i $@@@1M lib/limine/BOOTX64.EFI ::/EFI/BOOT; \
          mcopy -i $@@@1M lib/limine/BOOTIA32.EFI ::/EFI/BOOT

# Build a limine image with both bios and uefi boot options
targets += $(IMAGE_NAME)
$(IMAGE_NAME): artifacts/limine.conf $(BUILD)/limine $(BUILD)/kernel FORCE
	$(call cmd,mkimage)

quiet_cmd_run = QEMU    $<
      cmd_run = qemu-system-x86_64 \
                    --enable-kvm \
                    -cpu host,migratable=off \
                    -machine q35 \
                    -m 256M \
                    -smp 4 \
                    -s \
                    -drive format=raw,file=$<,media=disk \
                    -debugcon stdio \
                    -monitor telnet:127.0.0.1:5555,server,nowait \
                    -no-reboot \
                    -no-shutdown

PHONY += run
run: $(IMAGE_NAME)
	$(call cmd,run)
