CC=riscv64-unknown-elf-gcc -march=rv32im_zicsr -mabi=ilp32
CFLAGS= -ffreestanding -nostdlib
LDFLAGS=-T arch/riscv/linker.ld -lgcc
QEMU=qemu-system-riscv32
QFLAGS=-machine virt -nographic -bios none
