CC=zig cc -target aarch64-freestanding-none
CFLAGS=-mcpu=generic -mcmodel=large -mgeneral-regs-only -fno-sanitize=undefined
LDFLAGS=