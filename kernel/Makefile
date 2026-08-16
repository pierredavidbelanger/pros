
ARCH ?= aarch64

include config.$(ARCH).mk

.PHONY: default
default: bin/$(ARCH)/kernel

.PHONY: clean
clean:
	rm -rf bin

.PHONY: distclean
distclean: clean
	rm -rf libs

.PHONY: bin/$(ARCH)/kernel
bin/$(ARCH)/kernel: libs
	mkdir -p bin/$(ARCH)
	$(CC) \
		-Wall -std=gnu11 -nostdinc -ffreestanding -fno-stack-protector -fno-PIC \
		$(CFLAGS) \
		-isystem libs/freestanding-c-hdrs/include \
		-isystem libs/limine-protocol/include \
		-isystem libs/printf \
		-I include \
		libs/printf/printf.c \
		$(wildcard src/arch/$(ARCH)/*.S) \
		$(wildcard src/arch/$(ARCH)/*.c) \
		$(shell find src/core src/mm src/drivers src/fs src/proc src/syscall -name '*.c' 2>/dev/null) \
		-Wl,-T,src/arch/$(ARCH)/linker.lds \
		$(LDFLAGS) \
		-o bin/$(ARCH)/kernel

libs: libs/freestanding-c-hdrs libs/cc-runtime libs/limine-protocol libs/printf

libs/freestanding-c-hdrs:
	mkdir -p libs
	cd libs && git clone --depth 1 https://github.com/osdev0/freestanding-c-hdrs.git && rm -rf ./freestanding-c-hdrs/.git

libs/cc-runtime:
	mkdir -p libs
	cd libs && git clone --depth 1 https://github.com/osdev0/cc-runtime.git && rm -rf ./cc-runtime/.git

libs/limine-protocol:
	mkdir -p libs
	cd libs && git clone --depth 1 https://github.com/Limine-Bootloader/limine-protocol.git && rm -rf ./limine-protocol/.git

libs/printf:
	mkdir -p libs
	cd libs && git clone --depth 1 https://github.com/mpaland/printf && rm -rf ./printf/.git
