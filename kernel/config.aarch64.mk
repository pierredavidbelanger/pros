CC=../tools/zig-cc-clang
CFLAGS=-g -target aarch64-freestanding-none -mcpu=generic -mcmodel=large -mgeneral-regs-only -fno-sanitize=undefined
LDFLAGS=