# PjErOS (PrOS): Bare-Metal Operating System

PrOS is a minimalist, freestanding operating system kernel implemented in C for **AArch64 (ARM 64-bit)** and **x86_64 (AMD64 / Intel 64)**. It boots via **UEFI** using the **Limine Boot Protocol** and renders directly to a Limine-provided graphical framebuffer.

> [!NOTE]
> PrOS is a personal hobby project created to explore bare-metal programming, learn low-level system software development, and have fun building an OS from scratch! 
> AI IS utilized, but strictly as a tutor, mentor, debugger, rubber duck and code reviewer, not to generate slop I don't care about.

---

## 🛠️ Subsystems & Features

- **Architecture & Boot Protocol**:
  - Targets: **AArch64 (ARM 64-bit)** (`qemu-system-aarch64` with `virt` machine) and **x86_64 (AMD64)** (`qemu-system-x86_64` with `q35` machine), both booting via **UEFI** using EDK2 OVMF.
  - **Limine Protocol (v6)** integration handling Framebuffer, HHDM, Memory Map, Paging Mode, Executable Command Line, DTB (AArch64), RSDP/ACPI (x86_64), Stack Size, and Entry Point requests.
  - **Architecture Initialization**:
    - **AArch64**: Early boot activation of Coprocessor Access Control Register (`CPACR_EL1`) enabling AArch64 FPU/NEON hardware instructions.
    - **x86_64**: Higher-half kernel execution (`0xffffffff80000000`) compiled with `-mcmodel=kernel`, `-mno-red-zone`, and `-fno-sanitize=undefined`.
- **Physical Memory Management (PMM)**:
  - Page-frame allocator operating on 4 KiB pages (`PAGE_SIZE`).
  - Parses usable physical memory regions (`LIMINE_MEMMAP_USABLE`) from Limine and builds a linked free-list.
  - Supports single-page and multi-page contiguous page allocations (`pmm_alloc(count)`).
  - Translates physical to virtual addresses via the Higher-Half Direct Map (`HHDM`) offset.
  - API: `pmm_init()`, `pmm_alloc()`, `pmm_free()`, `pmm_phys_to_virt()`, `pmm_virt_to_phys()`.
- **Virtual Memory Management (VMM)**:
  - Architecture-neutral 4-level page-table walk — single source of truth for the level/index math, with real hardware differences (single-root x86_64 `CR3` vs. dual-root AArch64 `TTBR0_EL1`/`TTBR1_EL1`) delegated to per-architecture `arch_vmm_*` primitives instead of leaking into the shared walker.
  - Handles page mapping/unmapping, context switching, and a real per-context demand-paging policy (opt-in address range per `vmm_context`, decoded against the actual hardware fault reason rather than assuming every fault is legitimate).
  - Negotiates paging mode with Limine at boot and refuses to continue if it doesn't get 4-level paging.
  - Manages Higher-Half MMIO access faults (e.g., PCIe ECAM) mapped on demand from the HHDM window.
- **Kernel Dynamic Heap Allocator**:
  - First-fit linked free-list block allocator for sub-page and multi-page arbitrary byte allocations.
  - Guarantees 16-byte alignment (`HEAP_ALIGNMENT`) on every returned pointer, not merely on the requested size — the block header is padded up to `HEAP_HEADER_SIZE` so payload addresses inherit the alignment by construction.
  - Implements header metadata tracking (`struct heap_block`), automatic block splitting, forward coalescing on free, and dynamic heap expansion via PMM.
  - `krealloc()` grows in place where it can — absorbing free, physically contiguous neighbours — and falls back to allocate-copy-free only when it must.
  - Overflow-guarded: `kcalloc()` rejects `num * size` before it can wrap, and `kmalloc()` rejects sizes that would overflow its own alignment and page-count arithmetic.
  - API: `heap_init()`, `kmalloc()`, `kcalloc()`, `krealloc()`, `kfree()`.
- **Virtual File System (VFS)**:
  - Architecture-agnostic mount table with longest-prefix-match path resolution (`vfs_mount()`, `vfs_get_mountpoint()`).
  - Generic `struct vfs_node` / `vfs_ops` vtable (`create`, `open`, `close`, `read`, `write`, `finddir`, `readdir`) so any backing filesystem can plug in without touching the VFS core.
  - Path-walking helpers sharing a single tokenizing resolver: `vfs_lookup()` (find only), `vfs_create()` (find, or create the leaf), and `vfs_mkdir_parents()` (`mkdir -p` over a path's parent chain).
  - File descriptor table and Linux-style syscalls: `sys_open`, `sys_close`, `sys_read`, `sys_write`, `sys_readdir`, `sys_lseek`.
- **RAM Filesystem (ramfs) & initrd** *(work in progress)*:
  - `ramfs` is mounted at `/` before anything else and is currently the only filesystem — an in-memory tree with sibling-list directories, following Linux's `rootfs` pattern rather than the writable-tar-driver approach it replaced.
  - `tar_load()` parses a Limine-module-loaded **ustar `initrd.tar`** and populates ramfs purely through the public VFS path API, so the archive format never touches filesystem internals — the same separation as Linux's `unpack_to_rootfs()`.
  - **Current status**: the directory tree loads end to end — `/root/hello.txt` resolves through the mount table and `readdir` lists it. File *content* storage is the work in progress: nodes carry a sparse page list backed by the PMM (`void **pages`, a `NULL` entry being a hole that reads as zeros — the representation real `tmpfs` uses, picked so `mmap` can be layered on later), but the `read`/`write` implementations that fill it are still stubs.
  - The first-pass disk stack (VirtIO block driver, PCI/ACPI bus discovery, MBR partitioning, FAT16) was ripped out in favor of this.
- **Graphical Framebuffer & Terminal Engine**:
  - 32-bit ARGB/RGB direct pixel drawing and screen clearing (`fb_clear`).
  - Built-in 8x16 VGA bitmap font renderer supporting automatic text wrapping and full-screen vertical scrolling (`terminal_scroll`).
- **Kernel Logging & Panic Handling**:
  - Integrated `mpaland/printf` streaming formatted text through `_putchar` to the terminal engine.
  - `kprintf(tag, format, ...)` prefixes every line with a `[TAG  ]` padded to `KPRINTF_TAG_WIDTH`, so subsystem output lands in one column and the boot log stays greppable by tag.
  - Supports `kpanic` error reporting, and architecture-specific `khcf` (Halt and Catch Fire using `wfi` on ARM/RISC-V or `hlt` on x86_64).
- **Kernel Self-Tests**:
  - One file per subsystem under `kernel/core/test/`, all gated behind the `pros.tests` Limine command line so a normal boot stays quiet.
  - Shared `test_report(tag, name, ok)` prints one aligned `PASS`/`FAIL` line per check.
  - Currently 18 checks across PMM, heap (12, covering alignment, zeroing, overflow rejection, all three `krealloc` paths and block reuse), VMM and VFS.
- **Toolchain & Freestanding Runtime**:
  - Cross-compiles using `zig cc` (`-target aarch64-freestanding-none` or `-target x86_64-freestanding-none`).
  - Bundles freestanding C headers, compiler runtime, and custom `memcpy`, `memset`, `memmove`, and `memcmp` implementations.

---

## 📁 Project Structure

```text
.
├── Makefile               # Root build script (EFI System Partition setup, OVMF/Limine binaries, QEMU launchers)
├── limine.conf            # Limine bootloader configuration
├── README.md              # Project documentation
├── doc/                   # Roadmap & design working documents
└── kernel/                # Kernel source root
    ├── Makefile           # Kernel compilation script
    ├── arch/              # Architecture-specific code (aarch64, x86_64), linker scripts, exceptions, interrupts
    ├── core/              # Core kernel subsystems (boot, fb, console, kprintf, main) and test/ (one self-test file per subsystem)
    ├── fs/                # Filesystems: vfs/ (core + fd table), ramfs/ (in-memory root), tar/ (initrd loader)
    ├── include/           # Kernel headers (arch, core, fs, mm)
    ├── libs/              # Downloaded third-party libraries (freestanding headers, printf, limine)
    └── mm/                # Memory Management (pmm, vmm, heap)
```

---

## 🗺️ Development Roadmap & Status

Development happens in iterative phases, tracked entirely in [`doc/`](doc/ROADMAP.md) — `ROADMAP.md` holds the current phase breakdown and status, and each phase gets its own working document (currently `PHASE2_RAMFS.md`) while it's actively being designed or built. Superseded designs are kept and marked rather than deleted — `PHASE2_TAR_DRIVER.md` and `PHASE2_VFS.md` are both still there with banners explaining what replaced them and why. That's the source of truth for what's done and what's next, not this README.

AI is used extensively to help manage that folder — keeping the roadmap organized, writing up designs before any code gets touched, and tracking what's actually done versus still open. I write all the kernel code myself, that's the whole point of this project, but having a tutor around to explain the *why* behind a design, walk through how something actually works at the hardware level, and keep an honest paper trail of decisions (including the ones that got ripped out and redone) makes it a lot easier to stay oriented on a project this size, built a little at a time.

---

## 🛠️ Prerequisites

To build and run PrOS, ensure you have the following installed on your host:

1. **GNU Make** (`make`)
2. **Zig Compiler** ([`zig`](https://ziglang.org/)) – Used as the cross-compiler toolchain via `zig cc`.
3. **QEMU System Emulator**:
   - `qemu-system-aarch64` (for AArch64 target)
   - `qemu-system-x86_64` (for x86_64 target)
4. **Utilities**: `curl`, `tar`, `gunzip`, `ansifilter` (used by Makefile).

---

## 🚀 Building & Running

The quickest way in — builds both architectures and boots each one headless, back to back:

```bash
make
```

> [!NOTE]
> The build is quiet by default (`MAKEFLAGS += -s` in the root `Makefile`), so recipes aren't echoed and make's recursion banners are suppressed. This only silences make's *own* output — compiler diagnostics and `*** [Makefile:20: ...] Error 1` failures still print normally.

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
4. Build `initrd.tar` (a ustar archive of `initrd/`) and generate the bootable EFI System Partition structure in `root/` (Limine's EFI binary, `limine.conf`, the compiled kernel, and the initrd).
5. Launch QEMU with UEFI firmware, presenting `root/` as a `virtio-blk-pci` disk so OVMF can find and chainload it. That disk is used only by the firmware to boot — the kernel's own `/` is the in-memory ramfs, populated from the `initrd.tar` that Limine hands over as a module.

The kernel calls `arch_shutdown()` once boot finishes, so QEMU exits **on its own** — no need to close the window or kill the process manually.

### 2. Headless / No-Window Runs

For a fast, terminal-only run with no graphical window — useful over SSH or when you just want the boot log:

```bash
make qemu-x86_64-nographic
make qemu-aarch64-nographic

# Or both, one after the other — this is what a bare `make` runs
make qemu-both-nographic
```

Same boot as above, but with `-display none`, and the full serial output is also saved to `logs/qemu-<arch>.log` (gitignored) so you can inspect it afterward.

All four QEMU targets pipe their serial output through `ansifilter` before `tee`, stripping the terminal control sequences OVMF emits during firmware init. Without it the logs are littered with escape codes like `\033[2J\033[H`, which makes them painful to read and effectively impossible to `grep`.

### 3. Toggling Kernel Self-Tests

`test_pmm()`/`test_heap()`/`test_vmm()`/`test_vfs()` live one-per-file under `kernel/core/test/` and are gated on the Limine command line rather than being commented in/out of the source. Each `/Kernel (...)` entry in `limine.conf` carries `cmdline: pros.tests`, currently **enabled**:

```ini
/Kernel (ARM64)
    protocol: limine
    if_arch: aarch64
    cmdline: pros.tests
```

Comment that line out with `;` (Limine's comment marker) for a quiet boot. No kernel rebuild is required either way — `make` re-stages `root/` from the edited config on the next run.

Each check prints a single line through the shared `test_report()`:

```
[TEST ] [HEAP ] krealloc grow in place                 PASS
```

### 4. Manual Kernel Build Steps

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

The kernel shuts itself down at the end of boot, so QEMU normally exits on its own — nothing to do. If you need to bail out early (e.g. it's stuck, or you're debugging a hang), use the standard QEMU shortcuts:
- **Direct Exit**: Press `Ctrl+A` then `X`.
- **Monitor Console**: Press `Ctrl+A` then `C`, then type `q` and press `Enter`.


