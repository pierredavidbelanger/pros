
.PHONY: all
all: root

.PHONY: clean
clean:
	rm -rf root
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
	$(MAKE) -C kernel bin/kernel-aarch64

root: bin kernel
	mkdir -p root/boot/limine root/EFI/BOOT
	cp limine.conf root/boot/limine/
	cp bin/limine-binary/BOOT*.EFI root/EFI/BOOT/
	cp kernel/bin/kernel-* root/boot/

.PHONY: qemu-aarch64
qemu-aarch64: root
	qemu-system-aarch64 \
		-m 2G \
		-machine virt \
		-cpu cortex-a72 \
		-device ramfb \
		-drive if=pflash,unit=0,format=raw,file=bin/edk2-ovmf-bins/ovmf-code-aarch64.fd,readonly=on \
		-drive if=none,id=hd0,file=fat:rw:root,format=raw \
		-device virtio-blk-device,drive=hd0
