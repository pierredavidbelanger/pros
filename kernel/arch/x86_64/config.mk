CC=zig cc -target x86_64-freestanding-none
CFLAGS=-mcmodel=kernel -mno-red-zone -fno-sanitize=undefined
LDFLAGS=
