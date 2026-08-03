
ARCH ?= aarch64

include arch/$(ARCH)/config.mk

.PHONY: default
default: bin/kernel-$(ARCH)

.PHONY: clean
clean:
	rm -rf bin

.PHONY: distclean
distclean: clean
	rm -rf libs

.PHONY: bin/kernel-$(ARCH)
bin/kernel-$(ARCH): libs
	mkdir -p bin
	$(CC) \
		-Wall -std=gnu11 -nostdinc -ffreestanding -fno-stack-protector -fno-PIC \
		$(CFLAGS) \
		-isystem libs/freestanding-c-hdrs/include \
		-isystem libs/limine-protocol/include \
		-isystem libs/printf \
		-I include \
		libs/printf/printf.c \
		$(wildcard arch/$(ARCH)/*.S) \
		$(wildcard arch/$(ARCH)/*.c) \
		$(wildcard src/*.c) \
		-Wl,-T,arch/$(ARCH)/linker.lds \
		$(LDFLAGS) \
		-o bin/kernel-$(ARCH)

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
