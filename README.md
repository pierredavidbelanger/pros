# PjErOS (PrOS): Bare-Metal AArch64 Operating System

PrOS is a minimalist, freestanding operating system kernel implemented in C for **AArch64 (ARM 64-bit)**. It boots via **UEFI** using the **Limine Boot Protocol** and renders directly to a Limine-provided graphical framebuffer.

> [!NOTE]
> PrOS is a personal hobby project created to explore bare-metal programming, learn low-level system software development, and have fun building an OS from scratch! 
> AI IS utilized, but strictly as a tutor, mentor, debugger, rubber duck and code reviewer, not to generate slop I don't care about.

---

## 🛠️ Subsystems & Features

- **Architecture & Boot Protocol**:
  - Target: **AArch64 (ARM 64-bit)** booting via **UEFI** on QEMU `virt` machine using EDK2 OVMF.
  - **Limine Protocol (v6)** integration handling Framebuffer, HHDM, Memory Map, DTB, RSDP, Stack Size, and Entry Point requests.
  - Early boot activation of Coprocessor Access Control Register (`CPACR_EL1`) enabling AArch64 FPU/NEON hardware instructions.
- **Physical Memory Management (PMM)**:
  - Page-frame allocator operating on 4 KiB pages (`PAGE_SIZE`).
  - Parses usable physical memory regions (`LIMINE_MEMMAP_USABLE`) from Limine and builds a linked free-list.
  - Translates physical to virtual addresses via the Higher-Half Direct Map (`HHDM`) offset.
  - API: `pmm_init()`, `pmm_alloc()`, `pmm_free()`.
- **Graphical Framebuffer & Terminal Engine**:
  - 32-bit ARGB/RGB direct pixel drawing and screen clearing (`fb_clear`).
  - Built-in 8x16 VGA bitmap font renderer supporting automatic text wrapping and full-screen vertical scrolling (`terminal_scroll`).
- **Kernel Logging & Panic Handling**:
  - Integrated `mpaland/printf` streaming formatted text through `_putchar` to the terminal engine.
  - Supports `kprintf`, `kpanic` error reporting, and architecture-specific `khcf` (Halt and Catch Fire using `wfi` / `hlt`).
- **Toolchain & Freestanding Runtime**:
  - Cross-compiles using `zig cc` (`-target aarch64-freestanding-none`).
  - Bundles freestanding C headers, compiler runtime, and custom `memcpy`, `memset`, `memmove`, and `memcmp` implementations.

---

## 📁 Project Structure

```
.
├── Makefile               # Root build script (FAT root dir setup, OVMF/Limine binaries, QEMU launcher)
├── limine.conf            # Limine bootloader configuration
├── README.md              # Project documentation
├── AGENT.md               # Development & mentorship guidelines
└── kernel/                # Kernel source root
    ├── Makefile           # Kernel compilation script (auto-fetches freestanding libs & printf)
    ├── arch/              # Architecture configurations & linker scripts
    │   └── aarch64/       # AArch64 arch.c, config.mk, and linker.lds
    ├── include/           # Modular C headers (arch.h, boot.h, fb.h, kprintf.h, memory.h, pmm.h)
    └── src/               # Core kernel source
        ├── main.c         # Entry point (_start), initialization calls, kprintf logs
        ├── boot.c         # Limine boot protocol request structures (Framebuffer, HHDM, Memmap, DTB, RSDP, etc.)
        ├── fb.c           # Framebuffer driver, terminal engine, scrolling, font renderer
        ├── kprintf.c      # Formatted kprintf implementation (wraps mpaland/printf) and panic handling
        ├── memory.c       # Core C memory utilities (memcpy, memset, memmove, memcmp)
        └── pmm.c          # Physical Memory Manager (page frame allocator)
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
2. Download freestanding headers, runtime, limine protocol, and `mpaland/printf` into `kernel/libs/` (if missing).
3. Compile the kernel (`kernel/bin/kernel-aarch64`).
4. Generate the bootable FAT filesystem structure in `root/`.
5. Launch `qemu-system-aarch64` with RAMFB graphics, virtio drive, and UEFI firmware.

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
# Clean root directory and kernel build outputs
make clean

# Clean everything including downloaded third-party binaries and cloned libs
make distclean
```

---

## 💡 Exiting QEMU

When running QEMU in the terminal, exit using standard QEMU shortcuts:
- **Direct Exit**: Press `Ctrl+A` then `X`.
- **Monitor Console**: Press `Ctrl+A` then `C`, then type `q` and press `Enter`.

