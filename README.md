# PjErOS (PrOS): Bare-Metal AArch64 Operating System

PrOS is a minimalist, freestanding operating system kernel implemented in C for **AArch64 (ARM 64-bit)**. It boots via **UEFI** using the **Limine Boot Protocol** and renders directly to a Limine-provided graphical framebuffer.

---

## 🛠️ Features

- **Limine Boot Protocol (v6)**: Modular request handling (Framebuffer, DTB, ACPI/RSDP, Stack size) directly from Limine.
- **UEFI Booting & AArch64 Architecture**: Boots with EDK2 OVMF firmware on QEMU `virt` machine.
- **Zig Cross-Compilation Toolchain**: Built using `zig cc` (`-target aarch64-freestanding-none`), requiring no cross-GCC toolchain installation.
- **AArch64 FPU & SIMD Hardware Setup**: Early boot activation of Coprocessor Access Control Register (`CPACR_EL1`) enabling FPU/NEON hardware instructions.
- **Graphical Framebuffer & Terminal Engine**:
  - Direct 32-bit ARGB/RGB pixel manipulation and screen clearing (`fb_clear`).
  - Embedded 8x16 VGA bitmap font renderer with line wrapping and screen scrolling (`terminal_scroll`).
- **Formatted Kernel Logger (`kprintf`)**:
  - Integrated zero-dependency `mpaland/printf` formatting library streaming directly to terminal `_putchar`.
  - Supports `%s`, `%d`, `%x`, `%p`, `%u`, and custom format specifiers.
- **Freestanding C Library & Compiler Runtime**:
  - Modular integration of `freestanding-c-hdrs`, `cc-runtime`, `limine-protocol`, and `mpaland/printf`.
  - Custom freestanding `memcpy`, `memset`, `memmove`, and `memcmp`.

---

## 📁 Project Structure

```
.
├── Makefile               # Root build script (ISO creation, OVMF/Limine binaries, QEMU launcher)
├── limine.conf            # Limine bootloader configuration
├── README.md              # Project documentation
├── AGENT.md               # Development & mentorship guidelines
└── kernel/                # Kernel source root
    ├── Makefile           # Kernel compilation script (auto-fetches freestanding libs & printf)
    ├── arch/              # Architecture configurations & linker scripts
    │   └── aarch64/       # AArch64 config.mk and linker.lds
    ├── include/           # Modular C headers (arch.h, boot.h, fb.h, kprintf.h, memory.h)
    └── src/               # Core kernel source
        ├── main.c         # Entry point (kmain), initialization calls, kprintf logs
        ├── boot.c         # Limine boot protocol request structures (Framebuffer, DTB, RSDP, etc.)
        ├── fb.c           # Framebuffer driver, terminal engine, scrolling, font renderer
        ├── kprintf.c      # Formatted kprintf implementation (wraps mpaland/printf)
        └── memory.c       # Core C memory utilities (memcpy, memset, memmove, etc.)
```

---

## 🛠️ Prerequisites

To build and run PrOS, ensure you have the following installed on your host:

1. **GNU Make** (`make`)
2. **Zig Compiler** ([`zig`](https://ziglang.org/)) – Used as the cross-compiler toolchain via `zig cc`.
3. **QEMU System Emulator**:
   - `qemu-system-aarch64`
4. **Download Utilities**: `curl`, `tar`, `gunzip` (used by Makefile to automatically fetch Limine and OVMF firmware).

---

## 🚀 Building & Running

### 1. Build and Run in QEMU (Recommended)

From the root directory, simply run:

```bash
make qemu-aarch64
```

This single command will:
1. Download EDK2 OVMF AArch64 UEFI binaries and the Limine bootloader into `bin/` (if missing).
2. Download freestanding headers, runtime, and `mpaland/printf` into `kernel/libs/` (if missing).
3. Compile the kernel (`kernel/bin/kernel-aarch64`).
4. Generate the bootable FAT ISO structure (`iso/`).
5. Launch `qemu-system-aarch64` with RAMFB graphics, USB keyboard/tablet devices, and UEFI firmware.

### 2. Manual Kernel Build Steps

If you only want to compile the kernel binary without launching QEMU:

```bash
# Build the kernel binary (output: kernel/bin/kernel-aarch64)
make kernel

# Or build from inside the kernel directory
cd kernel
make
```

---

## 🧹 Cleaning Build Artifacts

```bash
# Clean ISO directory and kernel build outputs
make clean

# Clean everything including downloaded third-party binaries and cloned libs
make distclean
```

---

## 💡 Exiting QEMU

When running QEMU in the terminal, exit using standard QEMU shortcuts:
- **Direct Exit**: Press `Ctrl+A` then `X`.
- **Monitor Console**: Press `Ctrl+A` then `C`, then type `q` and press `Enter`.
