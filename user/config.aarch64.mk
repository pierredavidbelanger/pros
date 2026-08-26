CC=../tools/zig-cc-clang
CFLAGS=-target aarch64-linux-none
LDFLAGS=-nostdlib -static -Wl,--entry=_start -Wl,--image-base=0x40000000 -fno-sanitize=undefined
