CC=zig cc -target aarch64-linux-none
LDFLAGS=-nostdlib -static -Wl,--entry=_start -Wl,--image-base=0x40000000
