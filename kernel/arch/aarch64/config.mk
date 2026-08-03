CC=zig cc -target aarch64-freestanding-none
CFLAGS=-mcpu=generic -march=armv8-a+nofp+nosimd -mgeneral-regs-only
LDFLAGS=-m aarch64elf -T arch/aarch64/linker.lds