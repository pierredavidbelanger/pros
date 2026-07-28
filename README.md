# PjErOS (PrOS): Bare-Metal Multi-Architecture Operating System

PrOS is a minimalist, architecture-agnostic bare-metal kernel implemented in C and Assembly. It is designed as an educational, lightweight system targeting **32-bit ARM** (Cortex-A15) and **32-bit RISC-V** (RV32) processor architectures running in QEMU virtual machine environments.

---

## 🛠️ Features

- **Multi-Architecture Bootstrapping**: Unified C entry point (`kmain`) supporting ARM32 (`qemu-system-arm`) and RISC-V32 (`qemu-system-riscv32`).
- **Flattened Device Tree (FDT) Parsing**: Early boot parsing of the DTB binary passed by QEMU to dynamically discover RAM base address and size.
- **Freestanding C String & Format Library**: Custom implementations of `strlen`, `strcmp`, `strncmp`, `memset`, `memcpy`, `vsnprintf`, and `snprintf`.
- **Early Kernel Console & Diagnostics**: Formatted kernel output (`kprintf`) over UART and panic handling (`kpanic`).

---

## 🛠️ Prerequisites

Ensure you have the following installed on your system:

1. **Zig** (used as cross-compiler wrapper via `scripts/zig-cc`):
   - [Install Zig](https://ziglang.org/download/)
2. **CMake** (v3.20 or newer)
3. **QEMU System Emulators**:
   - `qemu-system-arm` (for ARM 32-bit execution)
   - `qemu-system-riscv32` (for RISC-V 32-bit execution)

---

## 🚀 Building & Running with CMake

Build targets are parameterized by the `ARCH` variable (`riscv` or `arm`).

### RISC-V (32-bit)

```bash
# Configure build
cmake -B cmake-build-riscv -DARCH=riscv

# Compile kernel
cmake --build cmake-build-riscv

# Run inside QEMU
cmake --build cmake-build-riscv --target run
```

### ARM (32-bit)

```bash
# Configure build
cmake -B cmake-build-arm -DARCH=arm

# Compile kernel
cmake --build cmake-build-arm

# Run inside QEMU
cmake --build cmake-build-arm --target run
```

### Cleaning Build Output

```bash
rm -rf cmake-build-riscv cmake-build-arm
```
