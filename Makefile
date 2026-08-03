
.PHONY: all
all: iso

.PHONY: clean
clean:
	rm -rf iso
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

iso: bin kernel
	mkdir -p iso/boot/limine iso/EFI/BOOT
	cp limine.conf iso/boot/limine/
	cp bin/limine-binary/BOOT*.EFI iso/EFI/BOOT/
	cp kernel/bin/kernel-* iso/boot/

.PHONY: qemu-aarch64
qemu-aarch64: iso
	qemu-system-aarch64 \
		-m 2G \
		-machine virt \
		-cpu cortex-a72 \
		-device ramfb \
		-drive if=pflash,unit=0,format=raw,file=bin/edk2-ovmf-bins/ovmf-code-aarch64.fd,readonly=on \
		-drive file=fat:rw:iso,format=raw
