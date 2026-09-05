CC=../tools/zig-cc-clang
CFLAGS=-DPRINTF_DISABLE_SUPPORT_FLOAT -g -target x86_64-freestanding-none -mcpu=x86_64-sse-sse2-mmx+soft_float -mcmodel=kernel -mno-red-zone -fno-sanitize=undefined
LDFLAGS=
