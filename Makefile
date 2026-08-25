
MAKEFLAGS += -s

.PHONY: default
default: qemu-both-nographic

.PHONY: all
all: kernel user

.PHONY: clean
clean:
	rm -rf bin/root logs
	$(MAKE) -C kernel clean
	$(MAKE) -C user clean
	$(MAKE) -C initrd clean

.PHONY: distclean
distclean: clean
	rm -rf bin
	$(MAKE) -C kernel distclean

bin/edk2-ovmf-bins:
	mkdir -p bin
	cd bin && curl -L https://github.com/osdev0/edk2-ovmf-stable-bins/releases/latest/download/edk2-ovmf-bins.tar.gz | gunzip | tar -xf -

bin/limine-binary:
	mkdir -p bin
	cd bin && curl -L https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz | gunzip | tar -xf - && cd limine-binary && make CC='zig cc'

.PHONY: kernel
kernel:
	$(MAKE) -C kernel ARCH=aarch64 bin/aarch64/kernel
	$(MAKE) -C kernel ARCH=x86_64 bin/x86_64/kernel

.PHONY: user
user:
	$(MAKE) -C user ARCH=aarch64
	$(MAKE) -C user ARCH=x86_64

.PHONY: initrd
initrd: user
	$(MAKE) -C initrd ARCH=aarch64
	$(MAKE) -C initrd ARCH=x86_64

bin/root: bin/edk2-ovmf-bins bin/limine-binary initrd kernel
	mkdir -p bin/root/boot/limine bin/root/EFI/BOOT bin/root/boot/x86_64 bin/root/boot/aarch64
	cp limine.conf bin/root/boot/limine/
	cp bin/limine-binary/BOOTAA64.EFI bin/root/EFI/BOOT/
	cp bin/limine-binary/BOOTX64.EFI bin/root/EFI/BOOT/
	cp kernel/bin/x86_64/kernel initrd/bin/x86_64/initrd bin/root/boot/x86_64/
	cp kernel/bin/aarch64/kernel initrd/bin/aarch64/initrd bin/root/boot/aarch64/

logs:
	mkdir -p logs

.PHONY: qemu-aarch64
qemu-aarch64: bin/root | logs
	qemu-system-aarch64 \
		-m 2G \
		-machine virt \
		-cpu cortex-a72 \
		-serial stdio \
		-drive if=pflash,unit=0,format=raw,file=bin/edk2-ovmf-bins/ovmf-code-aarch64.fd,readonly=on \
		-drive if=none,id=hd0,file=fat:rw:bin/root,format=raw \
		-device virtio-blk-pci,drive=hd0,disable-legacy=on \
		-device ramfb \
		| tee logs/qemu-aarch64.log

.PHONY: qemu-x86_64
qemu-x86_64: bin/root | logs
	qemu-system-x86_64 \
		-m 2G \
		-machine q35 \
		-serial stdio \
		-drive if=pflash,unit=0,format=raw,file=bin/edk2-ovmf-bins/ovmf-code-x86_64.fd,readonly=on \
		-drive if=none,id=hd0,file=fat:rw:bin/root,format=raw \
		-device virtio-blk-pci,drive=hd0,disable-legacy=on \
		| tee logs/qemu-x86_64.log

.PHONY: qemu-aarch64-nographic
qemu-aarch64-nographic: bin/root | logs
	mkdir -p logs
	qemu-system-aarch64 \
		-m 2G \
		-machine virt \
		-cpu cortex-a72 \
		-display none \
		-serial stdio \
		-drive if=pflash,unit=0,format=raw,file=bin/edk2-ovmf-bins/ovmf-code-aarch64.fd,readonly=on \
		-drive if=none,id=hd0,file=fat:rw:bin/root,format=raw \
		-device virtio-blk-pci,drive=hd0,disable-legacy=on \
		-device ramfb \
		| tee logs/qemu-aarch64.log

.PHONY: qemu-x86_64-nographic
qemu-x86_64-nographic: bin/root | logs
	mkdir -p logs
	qemu-system-x86_64 \
		-m 2G \
		-machine q35 \
		-display none \
		-serial stdio \
		-drive if=pflash,unit=0,format=raw,file=bin/edk2-ovmf-bins/ovmf-code-x86_64.fd,readonly=on \
		-drive if=none,id=hd0,file=fat:rw:bin/root,format=raw \
		-device virtio-blk-pci,drive=hd0,disable-legacy=on \
		| tee logs/qemu-x86_64.log

.PHONY: qemu-both-nographic
qemu-both-nographic: qemu-aarch64-nographic qemu-x86_64-nographic
