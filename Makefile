
.PHONY: default
default: root

.PHONY: clean
clean:
	rm -rf root logs
	$(MAKE) -C kernel clean

.PHONY: distclean
distclean: clean
	rm -rf bin
	$(MAKE) -C kernel distclean

bin: bin/edk2-ovmf-bins bin/limine-binary

bin/edk2-ovmf-bins:
	mkdir -p bin
	cd bin && curl -L https://github.com/osdev0/edk2-ovmf-stable-bins/releases/latest/download/edk2-ovmf-bins.tar.gz | gunzip | tar -xf -

bin/limine-binary:
	mkdir -p bin
	cd bin && curl -L https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz | gunzip | tar -xf - && cd limine-binary && make CC='zig cc'

.PHONY: kernel
kernel:
	$(MAKE) -C kernel ARCH=aarch64 bin/kernel-aarch64
	$(MAKE) -C kernel ARCH=x86_64 bin/kernel-x86_64

root: bin kernel
	mkdir -p root/boot/limine root/EFI/BOOT
	cp limine.conf root/boot/limine/
	cp bin/limine-binary/BOOTAA64.EFI root/EFI/BOOT/
	cp bin/limine-binary/BOOTX64.EFI root/EFI/BOOT/
	cp kernel/bin/kernel-* root/boot/

.PHONY: qemu-aarch64
qemu-aarch64: root
	qemu-system-aarch64 \
		-m 2G \
		-machine virt \
		-cpu cortex-a72 \
		-serial stdio \
		-drive if=pflash,unit=0,format=raw,file=bin/edk2-ovmf-bins/ovmf-code-aarch64.fd,readonly=on \
		-drive if=none,id=hd0,file=fat:rw:root,format=raw \
		-device virtio-blk-pci,drive=hd0,disable-legacy=on \
		-device ramfb \
		| tee logs/qemu-aarch64.log

.PHONY: qemu-x86_64
qemu-x86_64: root
	qemu-system-x86_64 \
		-m 2G \
		-machine q35 \
		-serial stdio \
		-drive if=pflash,unit=0,format=raw,file=bin/edk2-ovmf-bins/ovmf-code-x86_64.fd,readonly=on \
		-drive if=none,id=hd0,file=fat:rw:root,format=raw \
		-device virtio-blk-pci,drive=hd0,disable-legacy=on \
		| tee logs/qemu-x86_64.log

.PHONY: qemu-aarch64-nographic
qemu-aarch64-nographic: root
	mkdir -p logs
	qemu-system-aarch64 \
		-m 2G \
		-machine virt \
		-cpu cortex-a72 \
		-display none \
		-serial stdio \
		-drive if=pflash,unit=0,format=raw,file=bin/edk2-ovmf-bins/ovmf-code-aarch64.fd,readonly=on \
		-drive if=none,id=hd0,file=fat:rw:root,format=raw \
		-device virtio-blk-pci,drive=hd0,disable-legacy=on \
		-device ramfb \
		| tee logs/qemu-aarch64.log

.PHONY: qemu-x86_64-nographic
qemu-x86_64-nographic: root
	mkdir -p logs
	qemu-system-x86_64 \
		-m 2G \
		-machine q35 \
		-display none \
		-serial stdio \
		-drive if=pflash,unit=0,format=raw,file=bin/edk2-ovmf-bins/ovmf-code-x86_64.fd,readonly=on \
		-drive if=none,id=hd0,file=fat:rw:root,format=raw \
		-device virtio-blk-pci,drive=hd0,disable-legacy=on \
		| tee logs/qemu-x86_64.log

