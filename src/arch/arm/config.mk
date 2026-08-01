CC=arm-none-eabi-gcc
CFLAGS=-mcpu=cortex-a15 -ffreestanding -nostdlib
LDFLAGS=-T arch/arm/linker.ld -lgcc
QEMU=qemu-system-arm
QFLAGS=-machine virt -nographic
