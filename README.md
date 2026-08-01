# PjErOS (PrOS): Bare-Metal Multi-Architecture Operating System

PrOS is a minimalist, architecture-agnostic, hobby bare-metal kernel implemented in C and Assembly. It is designed as an educational, lightweight system targeting **32-bit ARM** (Cortex-A15) and **32-bit RISC-V** (RV32) processor architectures running in QEMU virtual machine environments.

---

## 🛠️ Features

- **Multi-Architecture Bootstrapping**: Unified C entry point (`kmain`) supporting ARM32 (`qemu-system-arm`) and RISC-V32 (`qemu-system-riscv32`).
- **Linux-Style Kernel Directory Layout**: Code structured into modular kernel subsystems (`arch`, `drivers`, `include`, `init`, `kernel`, `lib`, `mm`).
- **Flattened Device Tree (FDT) Parsing**: Early boot parsing of the DTB binary passed by QEMU using `smoldtb` to dynamically discover RAM base address and size.
- **Virtual & Physical Memory Management**: RISC-V 32-bit (Sv32) virtual memory page table setup and physical memory page allocation.
- **Freestanding C Utility Library**: Custom implementations of `strlen`, `strcmp`, `strncmp`, `memset`, `memcpy`, `memcmp`, `memchr`, `vsnprintf`, and `snprintf`.
- **Early Kernel Console & Diagnostics**: Formatted kernel output (`kprintf`) over UART and kernel panic handling (`kpanic`).

---

## 📁 Project Structure

```
.
├── Containerfile          # Container specification (Ubuntu-based cross-compilation environment)
├── Makefile               # Root Makefile for Podman container build workflows
├── README.md              # Project documentation
├── AGENT.md               # AI mentor guidelines
└── src/                   # Kernel source root
    ├── Makefile           # Main kernel build script
    ├── arch/              # Architecture-specific setup, bootloader & linker scripts
    │   ├── arm/           # ARM32 (Cortex-A15) config, arch.c, boot.S, linker.ld
    │   └── riscv/         # RISC-V32 (Sv32) config, arch.c, boot.S, linker.ld
    ├── drivers/           # Driver implementations
    │   └── of/            # Open Firmware / Device Tree parsing (of.c)
    ├── include/           # Modular C headers (asm/, drivers/, kernel/, lib/, mm/, uapi/)
    ├── init/              # Kernel entry point (init/main.c -> kmain)
    ├── kernel/            # Kernel core functions (printk.c)
    ├── lib/               # Utility libraries (smoldtb.c, string.c)
    └── mm/                # Memory management (mem.c)
```

---

## 🛠️ Prerequisites

You can build and run PrOS either using **Podman** (recommended for zero toolchain setup) or directly on your **Host System**.

### Option A: Podman Container Environment
1. **GNU Make** (`make`)
2. **Podman** ([Install Podman](https://podman.io/))

### Option B: Native Host Environment
Ensure you have the following installed:
1. **GNU Make** (`make`)
2. **GCC Cross Compilers**:
   - `arm-none-eabi-gcc` (for ARM 32-bit)
   - `riscv64-unknown-elf-gcc` (for RISC-V 32-bit)
3. **QEMU System Emulators**:
   - `qemu-system-arm`
   - `qemu-system-riscv32`

---

## 🚀 Building & Running

Build targets are parameterized by the `ARCH` variable (`riscv` [default] or `arm`).

### 1. Using Podman Container (Root Directory)

The root Makefile provides containerized build commands:

```bash
# (default) Launch an interactive shell inside the container with src/ mounted
make bash

# Build the container image (pros-build)
make build
```

Inside the container shell, the working directory is automatically set to `/usr/local/src/pros` (where `src/` is mounted), so you can directly follow the Native Build Workflow bellow.

---

### 2. Native Build Workflow (inside `src/` directory)

All kernel compilation and execution commands run from the `src/` directory.

#### RISC-V (32-bit, Default)

```bash
cd src

# (default) Compile and run inside QEMU (qemu-system-riscv32)
make run

# Compile kernel (output: build/riscv/kernel.elf)
make build

# Clean build artifacts
make clean
```

#### ARM (32-bit)

```bash
cd src

# Compile kernel for ARM (output: build/arm/kernel.elf)
make ARCH=arm build

# Compile and run ARM kernel inside QEMU (qemu-system-arm)
make ARCH=arm run

# Clean ARM build artifacts
make ARCH=arm clean
```

---

## 🧹 Cleaning Build Artifacts

```bash
cd src

# Clean current ARCH build output
make clean

# Clean specific architecture build output
make ARCH=riscv clean
make ARCH=arm clean
```

---

## 💡 Exiting QEMU

Since QEMU is executed in non-graphical terminal mode (`-nographic`), use the standard QEMU key combinations to exit:

- **Direct Exit**: Press `Ctrl+A` then `X` (press `Ctrl+a` together, release, then press `x`).
- **Via QEMU Monitor Console**: Press `Ctrl+A` then `C` to enter the QEMU monitor prompt (`(qemu)`), then type `q` (or `quit`) and press `Enter`.
