#!/bin/bash

rm -rf blob_*.o blob_*.bin

zig cc -target aarch64-freestanding-none -I ../kernel/include -c blob_aarch64.S -o blob_aarch64.o
zig cc -target x86_64-freestanding-none -I ../kernel/include -c blob_x86_64.S -o blob_x86_64.o

aarch64-linux-gnu-objcopy -O binary blob_aarch64.o blob_aarch64.bin
aarch64-linux-gnu-objcopy -O binary blob_x86_64.o blob_x86_64.bin

xxd -i blob_aarch64.bin
xxd -i blob_x86_64.bin
