CC=../tools/zig-cc-clang
CFLAGS=-DPRINTF_DISABLE_SUPPORT_FLOAT -g -target aarch64-freestanding-none -mcpu=generic-fp_armv8-neon -mcmodel=large -fno-sanitize=undefined
LDFLAGS=