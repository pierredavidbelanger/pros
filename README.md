# PjErOS (PrOS): Bare-Metal Operating System

PrOS is a minimalist, freestanding operating system kernel implemented in C for **AArch64 (ARM 64-bit)** and **x86_64 (AMD64 / Intel 64)**. It boots via **UEFI** using the **Limine Boot Protocol** and renders directly to a Limine-provided graphical framebuffer.

> [!NOTE]
> PrOS is a personal hobby project created to explore bare-metal programming, learn low-level system software development, and have fun building an OS from scratch! 
> AI IS utilized as a tutor, mentor, debugger, rubber duck, code reviewer and doc writer, not to generate slop I don't care about.
> One deliberate exception: the self-test suites under `kernel/src/core/test/` are mostly AI-written. I love tests, just not enough to write them.
> I try to keep the AI commit attributions, so modifications lands with co-author trailers so the git history says plainly who wrote what.

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
| **Exceptions & interrupts** | IDT with assembly ISR stubs on x86_64, EL1 vector table on AArch64. Page faults route into the VMM's demand pager; anything genuinely unhandled dumps full register state *and every task's state* before halting, so a crash is diagnosable from the boot log alone. |
| **Interrupt controllers & timer** | 8259 PIC (remapped off the exception vectors) plus the 8254 PIT on x86_64, with the local APIC put into virtual wire mode so the PIC's output actually reaches the CPU; GICv2 distributor and CPU interface plus the ARM Generic Timer on AArch64. Both feed one architecture-neutral tick counter at 100 Hz — the hardware differs, "how many times has it fired" does not. |
| **Tasks & preemptive scheduler** | Round-robin over a circular run queue, driven only by the timer interrupt — no task yields, nothing is cooperative. The trap stub already saves every register on entry, so a context switch is just *returning a different frame than the one you were handed*: the C handler returns the frame to restore, and the same four lines of assembly that resume an interrupted task also start a brand-new one. A new task has never trapped, so its first frame is **fabricated by hand** to look exactly like one the hardware would have written (`iretq`'s `rip`/`cs`/`rflags`/`rsp`/`ss`, `eret`'s `elr`/`spsr`) — the one place the two architectures genuinely diverge. The decision to switch (`need_resched`, set in the timer path) and the act of switching (trap exit) are deliberately separate, so the kernel is *preempted at trap exit* rather than preemptible, which is far easier to get right. |
| **Kernel stacks** | One 16 KiB stack per task with a guard value at the base, so an overflow panics instead of quietly eating a neighbour. Per-task rather than one global exception stack, because sharing a stack across tasks corrupts memory in proportion to how *well* the scheduler is working. `task_dump()` reports each stack's extent, guard status and high-water mark. |
| **Virtual filesystem (VFS)** | Mount table with longest-prefix matching, a `vfs_ops` vtable any filesystem can implement, one shared path resolver behind `vfs_lookup`/`vfs_create`/`vfs_mkdir_parents`, and a file-descriptor table under Linux-shaped syscalls (`sys_open`, `sys_read`, `sys_lseek`, …). |
| **ramfs + initrd loader** | `ramfs` owns storage and is mounted at `/`; `tar_load()` is *only* a ustar parser that fills it through the public VFS API — the same separation as Linux's `rootfs` + `unpack_to_rootfs()`, so the archive format never touches filesystem internals. Files are read/write, stored as a sparse page list where an unallocated page reads back as zeros, so seeking past the end and writing produces a real hole rather than megabytes of stored nothing. |
| **Console** | Output fans out to the serial port (live from the very first line of boot) and to the framebuffer terminal once it exists, so the boot log reads identically on screen and over `-serial stdio`. |
| **Framebuffer terminal** | Direct 32-bit pixel drawing plus an 8x16 bitmap font renderer with line wrapping and scrolling. UEFI leaves no hardware text mode behind, so the terminal is drawn by hand, pixel by pixel. |
| **Logging** | `kprintf(tag, ...)` stamps every line with a padded `[SUBSYS]` prefix, keeping the boot log aligned in one column and greppable by subsystem. `kpanic` reports and halts. |
| **Self-tests** | One file per subsystem under `core/test/`, gated behind a `pros.tests` kernel command line so a normal boot stays quiet. Each check prints a single `PASS`/`FAIL` line — 75 of them today, across PMM, heap, VMM, VFS, kernel stacks and tasks, including the sparse-file and page-boundary paths the initrd loader can never reach on its own. |
| **Freestanding runtime** | Cross-compiled with `zig cc`, no libc, no host headers. Bundles the `mem*`/`str*` routines the kernel actually uses — `strcpy`/`strncpy` are deliberately absent, since neither guarantees a terminated result. |

> [!NOTE]
> There is no disk driver. An earlier VirtIO-block / PCI / FAT16 stack was built, judged not clean enough, and removed in favour of the RAM-backed initrd above. That history is kept in [`doc/archive/PHASE2_VFS.md`](doc/archive/PHASE2_VFS.md) rather than deleted.

---

## 📁 Project Structure

Three sibling, architecture-parametrized subprojects (`kernel/`, `user/`, `initrd/`), each with
its own `Makefile` and `config.$(ARCH).mk`, orchestrated by the root `Makefile` — the only place
that knows the cross-subproject build order (`user` → `initrd` → the assembled boot image).
Every build artifact, from every subproject, lands under `bin/`.

```text
.
├── Makefile               # Orchestrates kernel/user/initrd, OVMF/Limine downloads, QEMU launchers
├── limine.conf            # Limine bootloader configuration (per-arch kernel/initrd paths)
├── README.md              # Project documentation
├── doc/                   # Roadmap & the working document for the phase being designed now
│   └── archive/           # Finished & superseded working documents, kept for the reasoning trail
├── kernel/                # Kernel source root
│   ├── Makefile           # ARCH-parametrized build → kernel/bin/$(ARCH)/kernel
│   ├── config.x86_64.mk, config.aarch64.mk   # Per-arch CC/CFLAGS/LDFLAGS
│   ├── include/           # Kernel headers (arch, core, fs, mm, proc)
│   └── src/               # Kernel sources
│       ├── arch/          # Architecture-specific code (aarch64, x86_64), linker scripts, exceptions, interrupts
│       ├── core/          # Core kernel subsystems (boot, fb, console, kprintf, main) and test/ (one self-test file per subsystem)
│       ├── fs/            # Filesystems: vfs/ (core + fd table), ramfs/ (in-memory root), tar/ (initrd loader)
│       ├── mm/            # Memory Management (pmm, vmm, heap)
│       ├── proc/          # Tasks & scheduling (task, sched, kstack)
│       └── syscall/       # Syscall dispatch table
├── user/                  # Userland programs, one subdirectory per program
│   ├── Makefile           # ARCH-parametrized, auto-discovers src/*/ → user/bin/$(ARCH)/<program>
│   ├── config.x86_64.mk, config.aarch64.mk   # Per-arch CC/LDFLAGS (freestanding, linux-none target)
│   └── src/init/          # /bin/init — no libc, inline-asm syscall wrappers
└── initrd/                # The RAM-backed root filesystem's tracked source content
    ├── Makefile           # Packs data/ + that arch's user/ binaries into initrd/bin/$(ARCH)/initrd
    └── data/root/         # Tracked filesystem content (e.g. hello.txt) — the name IS the runtime /root path
```

`kernel/libs/` (downloaded third-party libraries — freestanding headers, printf, limine) is
gitignored build output, same as every subproject's `bin/`.

---

## 🗺️ Development Roadmap & Status

Development happens in iterative phases, tracked entirely in [`doc/`](doc/ROADMAP.md) — `ROADMAP.md` holds the phase breakdown and status, and each phase gets its own working document once it's being designed or built. Every phase is sized to end in something demonstrable rather than merely correct, and the roadmap states that payoff up front.

Phases 1 through 4 are done. Phase 3 ([`PHASE3_PREEMPTION.md`](doc/archive/PHASE3_PREEMPTION.md)) closed with the payoff it was shaped around: **preemptive multitasking**. Three tasks — the boot task plus two kernel threads — take turns on the CPU driven only by the timer interrupt, their output interleaves, and each one's switch counter lands within a few of the others after three seconds at 100 Hz. Nothing yields; nothing is cooperative. It all happens in ring 0, deliberately, because that's where a mistake prints a register dump instead of resetting the machine.

Phase 4 ([`PHASE4_PRIVILEGE.md`](doc/archive/PHASE4_PRIVILEGE.md)) closed with *its* payoff: **a program the kernel does not trust calls `write` and the string appears on the console.** Step 1 dropped to ring 3 / EL0 — a hand-fabricated user program runs unprivileged and faults on a privileged instruction, with three follow-up experiments confirming the CPU actually enforces it rather than just claiming to ([`PHASE4_STEP1_RING3_EL0.md`](doc/archive/PHASE4_STEP1_RING3_EL0.md)). Step 2 built the syscall boundary itself — `svc` dispatch on AArch64, and `syscall`/`sysret` + `swapgs` + a hand-built trap frame on x86_64 — and wired exactly one syscall, `write`, dispatched through a table indexed by Linux syscall numbers ([`PHASE4_STEP2_SYSCALL.md`](doc/archive/PHASE4_STEP2_SYSCALL.md)). The blob calls `write(1, "hello from ring 3\n", 18)` from ring 3 / EL0, the string appears on the console, and the same task then deliberately faults on a privileged instruction, proving the machine survived the syscall cleanly. Phase 5 ([`PHASE5_LOADING.md`](doc/PHASE5_LOADING.md)) is next, adding the ELF loader and a real `/bin/init`: the freestanding `/bin/init` binary itself now builds and boots for both architectures, the loader that will actually run it is not yet written. From there the roadmap runs through an interactive shell, `fork`/`exec`, `/dev/fb0`, BusyBox, PTYs, X11 and finally a web browser.

Large Steps get their own document while being built, then move to the archive when they close — most recently [`PHASE3_STEP3_ROUND_ROBIN.md`](doc/archive/PHASE3_STEP3_ROUND_ROBIN.md), whose retrospective records where the build diverged from the design and the two failures that cost the most time. A phase document joins them once its last Step lands. `ROADMAP.md` also records the naming convention these documents follow: Phase → Step → Part, with Chapters inside a phase document. That folder is the source of truth for what's done and what's next, not this README.

Finished and superseded designs move to [`doc/archive/`](doc/archive/) rather than being deleted — *why* a design was abandoned tends to outlive the design itself. `PHASE2_VFS.md` (the disk-backed stack that got ripped out) and `PHASE2_STEP13_TAR_DRIVER.md` (the writable-tar design that became ramfs) both carry banners explaining what replaced them. Splitting them out keeps `doc/` down to just what's live, so designing the next thing doesn't mean re-reading the last three.

AI is used extensively to help manage that folder — keeping the roadmap organized, writing up designs before any code gets touched, and tracking what's actually done versus still open.

---

## 🛠️ Prerequisites

To build and run PrOS, ensure you have the following installed on your host:

1. **GNU Make** (`make`)
2. **Zig Compiler** ([`zig`](https://ziglang.org/)) – Used as the cross-compiler toolchain via `zig cc`.
3. **QEMU System Emulator**:
   - `qemu-system-aarch64` (for AArch64 target)
   - `qemu-system-x86_64` (for x86_64 target)
4. **Utilities**: `curl`, `tar`, `gunzip` (used by Makefile).

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
3. Compile the kernel for both architectures (`kernel/bin/x86_64/kernel`, `kernel/bin/aarch64/kernel`).
4. Compile userland programs for both architectures (`user/bin/x86_64/init`, `user/bin/aarch64/init`, and so on for any others under `user/src/`).
5. Pack each architecture's `initrd/data/` content plus that architecture's userland binaries into its own archive (`initrd/bin/x86_64/initrd`, `initrd/bin/aarch64/initrd`).
6. Assemble the bootable EFI System Partition structure in `bin/root/` — Limine's EFI binary, `limine.conf`, and each architecture's kernel + initrd under `bin/root/boot/$(ARCH)/`.
7. Launch QEMU with UEFI firmware, presenting `bin/root/` as a `virtio-blk-pci` disk so OVMF can find and chainload it. That disk is used only by the firmware to boot — the kernel's own `/` is the in-memory ramfs, populated from the matching-architecture `initrd` that Limine hands over as a module.

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

All four QEMU targets pipe their serial output straight to `tee` — nothing sits in between. An earlier pipeline ran it through `ansifilter` to strip the terminal control sequences OVMF and Limine emit during firmware init, but a filter in the pipe buffers, and a kernel that panics never closes the pipe: `kpanic()` ends in `arch_halt()`, QEMU never exits, and killing the run discards up to 4 KB of buffered output — reliably eating the panic message you were trying to read. The escape codes are the lesser problem, so they stay. `console_init()` starts the kernel's output on a fresh line rather than clearing the screen, which keeps the firmware's own boot messages readable and leaves every kernel line anchored at column 0 for `grep`.

### 3. Toggling Kernel Self-Tests

`test_pmm()`/`test_heap()`/`test_vmm()`/`test_vfs()`/`test_kstack()`/`test_task()` live one-per-file under `kernel/src/core/test/` and are gated on the Limine command line rather than being commented in/out of the source. Each `/Kernel (...)` entry in `limine.conf` carries `cmdline: pros.tests`, currently **enabled**:

```ini
/Kernel (ARM64)
    protocol: limine
    if_arch: aarch64
    cmdline: pros.tests
```

Comment that line out with `;` (Limine's comment marker) for a quiet boot. No kernel rebuild is required either way — `make` re-stages `bin/root/` from the edited config on the next run.

Each check prints a single line through the shared `test_report()`:

```
[TEST ] [HEAP ] krealloc grow in place                 PASS
```

### 4. Manual Subproject Build Steps

Each subproject (`kernel/`, `user/`, `initrd/`) is independently buildable without launching QEMU:

```bash
# Build one subproject for both architectures (outputs land under <subproject>/bin/$(ARCH)/)
make kernel
make user
make initrd    # depends on `user` — packs that arch's userland binaries into the initrd archive

# Or build a specific architecture from inside a subproject directory
cd kernel
make ARCH=x86_64
make ARCH=aarch64
```

Cross-subproject build order (`user` before `initrd`, both before the assembled `bin/root/`) is
the root `Makefile`'s job — each subproject's own `Makefile` only knows how to build itself, so
running one standalone (e.g. `make -C initrd ARCH=x86_64`) before its dependency is built fails
with a clear error rather than silently producing something incomplete.

---

## 🧹 Cleaning Build Artifacts

```bash
# Clean every subproject's build output plus the assembled boot image
make clean

# Clean everything including downloaded third-party binaries and cloned libs
make distclean
```

---

## 💡 Exiting QEMU

The kernel shuts itself down at the end of boot, so QEMU normally exits on its own — nothing to do. If you need to bail out early (e.g. it's stuck, or you're debugging a hang), use the standard QEMU shortcuts:
- **Direct Exit**: Press `Ctrl+A` then `X`.
- **Monitor Console**: Press `Ctrl+A` then `C`, then type `q` and press `Enter`.


