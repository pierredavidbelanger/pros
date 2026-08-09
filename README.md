# PjErOS (PrOS): Bare-Metal Operating System

PrOS is a minimalist, freestanding operating system kernel implemented in C for **AArch64 (ARM 64-bit)** and **x86_64 (AMD64 / Intel 64)**. It boots via **UEFI** using the **Limine Boot Protocol** and renders directly to a Limine-provided graphical framebuffer.

> [!NOTE]
> PrOS is a personal hobby project created to explore bare-metal programming, learn low-level system software development, and have fun building an OS from scratch! 
> AI IS utilized, but strictly as a tutor, mentor, debugger, rubber duck and code reviewer, not to generate slop I don't care about.

---

## 🛠️ Subsystems & Features

Everything below is built and boots on **both** architectures. Sources are in `kernel/`; the working documents in [`doc/`](doc/ROADMAP.md) carry the full reasoning behind each design.

| Subsystem | What it does, and why it's built that way |
| --- | --- |
| **Dual-architecture boot** | One kernel source tree targeting AArch64 (`virt`) and x86_64 (`q35`), booted by Limine over EDK2 OVMF firmware. CPU differences sit behind an `arch_*` interface rather than `#ifdef`s scattered through shared code. |
| **Limine protocol (v6)** | Consumes framebuffer, HHDM, memory map, paging mode, kernel command line, initrd module, and DTB/RSDP responses. Letting the bootloader solve bootloader problems keeps the kernel focused on what happens after. |
| **Physical memory (PMM)** | 4 KiB page-frame allocator over a free-list built from Limine's memory map, supporting contiguous multi-page allocation and physical↔virtual translation through the higher-half direct map. |
| **Virtual memory (VMM)** | Architecture-neutral 4-level page-table walker, with the genuine hardware split (x86_64's single `CR3` vs AArch64's `TTBR0`/`TTBR1` pair) isolated behind `arch_vmm_*`. Handles mapping, per-context switching, and opt-in demand paging that decodes the *actual* fault reason instead of assuming every fault is legitimate. |
| **Kernel heap** | First-fit free-list allocator with block splitting, coalescing on free, and a `krealloc` that grows in place by absorbing a free adjacent block. Alignment is guaranteed on the returned pointer (not merely on the requested size), and every size calculation is overflow-checked. |
| **Exceptions & interrupts** | IDT with assembly ISR stubs on x86_64, EL1 vector table on AArch64. Page faults route into the VMM's demand pager; anything genuinely unhandled dumps full register state before halting, so a crash is diagnosable from the boot log alone. |
| **Virtual filesystem (VFS)** | Mount table with longest-prefix matching, a `vfs_ops` vtable any filesystem can implement, one shared path resolver behind `vfs_lookup`/`vfs_create`/`vfs_mkdir_parents`, and a file-descriptor table under Linux-shaped syscalls (`sys_open`, `sys_read`, `sys_lseek`, …). |
| **ramfs + initrd loader** 🚧 | `ramfs` owns storage and is mounted at `/`; `tar_load()` is *only* a ustar parser that fills it through the public VFS API — the same separation as Linux's `rootfs` + `unpack_to_rootfs()`, so the archive format never touches filesystem internals. The directory tree loads today; file content storage is the work in progress. |
| **Console** | Output fans out to the serial port (live from the very first line of boot) and to the framebuffer terminal once it exists, so the boot log reads identically on screen and over `-serial stdio`. |
| **Framebuffer terminal** | Direct 32-bit pixel drawing plus an 8x16 bitmap font renderer with line wrapping and scrolling. UEFI leaves no hardware text mode behind, so the terminal is drawn by hand, pixel by pixel. |
| **Logging** | `kprintf(tag, ...)` stamps every line with a padded `[SUBSYS]` prefix, keeping the boot log aligned in one column and greppable by subsystem. `kpanic` reports and halts. |
| **Self-tests** | One file per subsystem under `core/test/`, gated behind a `pros.tests` kernel command line so a normal boot stays quiet. Each check prints a single `PASS`/`FAIL` line — 18 of them today, across PMM, heap, VMM and VFS. |
| **Freestanding runtime** | Cross-compiled with `zig cc`, no libc, no host headers. Bundles the `mem*`/`str*` routines the kernel actually uses — `strcpy`/`strncpy` are deliberately absent, since neither guarantees a terminated result. |

> [!NOTE]
> There is no disk driver. An earlier VirtIO-block / PCI / FAT16 stack was built, judged not clean enough, and removed in favour of the RAM-backed initrd above. That history is kept in [`doc/PHASE2_VFS.md`](doc/PHASE2_VFS.md) rather than deleted.

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


