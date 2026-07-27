# PjErOS (PrOS): Bare-Metal Multi-Architecture Operating System

PrOS is a minimalist, architecture-agnostic bare-metal kernel implemented in C and Assembly. It is designed as an educational (for me), lightweight system targeting **32-bit ARM** and **32-bit RISC-V** processor architectures running in QEMU virtual machine environments.

---

## 🛠️ Prerequisites

To compile and run the project, ensure you have the following installed on your system:

1. **Zig** (used for `zig cc` cross-compiler):
   - [Install Zig](https://ziglang.org/download/)
2. **QEMU System Emulators**:
   - `qemu-system-arm` (for ARM 32-bit execution)
   - `qemu-system-riscv32` (for RISC-V 32-bit execution)
3. **GNU Make**

---

## 🚀 Building & Running

Build and run commands are parameterised by the `ARCH` variable (`arm` or `riscv`).

### Running ARM (32-bit)

To compile the kernel and run it inside `qemu-system-arm`:

```bash
make ARCH=arm run
```

### Running RISC-V (32-bit)

To compile the kernel and run it inside `qemu-system-riscv32`:

```bash
make ARCH=riscv run
```

### Clean Build Targets

To clean compiled kernel binaries:

```bash
make ARCH=arm clean
# or
make ARCH=riscv clean
# or just
make clean
```
