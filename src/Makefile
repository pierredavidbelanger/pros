
ARCH ?= riscv

include arch/$(ARCH)/config.mk

SRC := \
	$(wildcard arch/$(ARCH)/*.c) \
	$(wildcard arch/$(ARCH)/*.S) \
	drivers/of/of.c \
	init/main.c \
	kernel/printk.c \
	lib/smoldtb.c \
	lib/string.c \
	mm/mem.c

run: build
	$(QEMU) $(QFLAGS) -kernel build/$(ARCH)/kernel.elf

.PHONY: build
build: clean
	mkdir -p build/$(ARCH)
	$(CC) -Wall -Wextra -Wconversion -Wshadow -Wformat=2 $(CFLAGS) -Iinclude $(SRC) $(LDFLAGS) -o build/$(ARCH)/kernel.elf

clean:
	rm -rf build/$(ARCH)
