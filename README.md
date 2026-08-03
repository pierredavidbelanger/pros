# PjErOS (PrOS): Bare-Metal Operating System

PrOS is a minimalist, freestanding operating system kernel implemented in C for **AArch64 (ARM 64-bit)** and **x86_64 (AMD64 / Intel 64)**. It boots via **UEFI** using the **Limine Boot Protocol** and renders directly to a Limine-provided graphical framebuffer.

> [!NOTE]
> PrOS is a personal hobby project created to explore bare-metal programming, learn low-level system software development, and have fun building an OS from scratch! 
> AI IS utilized, but strictly as a tutor, mentor, debugger, rubber duck and code reviewer, not to generate slop I don't care about.

---

## 🛠️ Subsystems & Features

- **Architecture & Boot Protocol**:
  - Targets: **AArch64 (ARM 64-bit)** (`qemu-system-aarch64` with `virt` machine) and **x86_64 (AMD64)** (`qemu-system-x86_64` with `q35` machine), both booting via **UEFI** using EDK2 OVMF.
  - **Limine Protocol (v6)** integration handling Framebuffer, HHDM, Memory Map, DTB (AArch64), RSDP/ACPI (x86_64), Stack Size, and Entry Point requests.
  - **Architecture Initialization**:
    - **AArch64**: Early boot activation of Coprocessor Access Control Register (`CPACR_EL1`) enabling AArch64 FPU/NEON hardware instructions.
    - **x86_64**: Higher-half kernel execution (`0xffffffff80000000`) compiled with `-mcmodel=kernel`, `-mno-red-zone`, and `-fno-sanitize=undefined`.
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
  - Supports `kprintf`, `kpanic` error reporting, and architecture-specific `khcf` (Halt and Catch Fire using `wfi` on ARM/RISC-V or `hlt` on x86_64).
- **Toolchain & Freestanding Runtime**:
  - Cross-compiles using `zig cc` (`-target aarch64-freestanding-none` or `-target x86_64-freestanding-none`).
  - Bundles freestanding C headers, compiler runtime, and custom `memcpy`, `memset`, `memmove`, and `memcmp` implementations.

---

## 📁 Project Structure

```
.
├── Makefile               # Root build script (FAT root dir setup, OVMF/Limine binaries, QEMU launchers)
├── limine.conf            # Limine bootloader configuration
├── README.md              # Project documentation
├── AGENT.md               # Development & mentorship guidelines
└── kernel/                # Kernel source root
    ├── Makefile           # Kernel compilation script (auto-fetches freestanding libs & printf)
    ├── arch/              # Architecture configurations & linker scripts
    │   ├── aarch64/       # AArch64 arch.c, config.mk, and linker.lds
    │   └── x86_64/        # x86_64 arch.c, config.mk, and linker.lds
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
   - `qemu-system-aarch64` (for AArch64 target)
   - `qemu-system-x86_64` (for x86_64 target)
4. **Download Utilities**: `curl`, `tar`, `gunzip` (used by Makefile to automatically fetch Limine and OVMF firmware).

---

## 🚀 Building & Running

### 1. Build and Run in QEMU

Run for **AArch64**:

```bash
make qemu-aarch64
```

Run for **x86_64**:

```bash
make qemu-x86_64
```

These commands will:
1. Download EDK2 OVMF UEFI binaries (AArch64 / x86_64) and the Limine bootloader into `bin/` (if missing).
2. Download freestanding headers, runtime, limine protocol, and `mpaland/printf` into `kernel/libs/` (if missing).
3. Compile the kernel (`kernel/bin/kernel-aarch64` and `kernel/bin/kernel-x86_64`).
4. Generate the bootable FAT filesystem structure in `root/`.
5. Launch QEMU with UEFI firmware and appropriate virtio devices (`virtio-blk-device` on ARM64 / `virtio-blk-pci` on x86_64).

### 2. Manual Kernel Build Steps

If you only want to compile the kernel binaries without launching QEMU:

```bash
# Build kernel binaries for both architectures (outputs: kernel/bin/kernel-aarch64 & kernel/bin/kernel-x86_64)
make kernel

# Or build a specific target architecture from inside kernel/
cd kernel
make ARCH=x86_64
make ARCH=aarch64
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


