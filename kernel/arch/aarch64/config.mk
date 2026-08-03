CC=zig cc -target aarch64-freestanding-none
CFLAGS=-mcpu=generic -mgeneral-regs-only -fno-sanitize=undefined
LDFLAGS=