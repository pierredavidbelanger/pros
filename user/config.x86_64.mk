CC=../tools/zig-cc-clang
CFLAGS=-target x86_64-linux-none
LDFLAGS=-nostdlib -static -Wl,--entry=_start -Wl,--image-base=0x40000000
