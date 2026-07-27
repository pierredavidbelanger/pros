
-include arch/$(ARCH)/config.mk

CC := zig cc

SRCS := src/kernel.c arch/$(ARCH)/boot.S arch/$(ARCH)/uart.c
LINKER_SCRIPT := arch/$(ARCH)/linker.ld

compile:
	mkdir -p target/$(ARCH)
	$(CC) $(CFLAGS) -fno-sanitize=undefined -ffreestanding -Iinclude -I. -T $(LINKER_SCRIPT) $(SRCS) -o target/$(ARCH)/kernel.elf

run: compile
	$(QEMU) $(QEMU_FLAGS) -machine virt -nographic --no-reboot -kernel target/$(ARCH)/kernel.elf

clean:
	rm -rf target/$(ARCH)
