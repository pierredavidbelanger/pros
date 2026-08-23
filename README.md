# PjErOS (PrOS): Bare-Metal Operating System

PrOS is a minimalist, freestanding operating system kernel implemented in C for **AArch64 (ARM 64-bit)** and **x86_64 (AMD64 / Intel 64)**. It boots via **UEFI** using the **Limine Boot Protocol** and renders directly to a Limine-provided graphical framebuffer.

PrOS is a personal hobby project created to explore bare-metal programming, learn low-level system software development, and have fun building an OS from scratch!

> [!NOTE]
> AI *is* utilized as a tutor, mentor, debugger, rubber duck, code reviewer and doc writer, not to generate slop I don't care about.
> A deliberate exception: the self-tests that are mostly AI-written. I love tests, just not enough to write them.
> I try to keep the AI commit attributions, so modifications lands with co-author trailers, the git history says plainly who wrote what.

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
| **Console** | Output fans out to the serial port (live from the very first line of boot) and to the framebuffer terminal once it exists, so the boot log reads identically on screen and over `-serial stdio`. Input arrives the other way: a UART receive interrupt (16550 IRQ 4 on `q35`, PL011 SPI 33 on `virt`) pushes bytes into one shared, architecture-neutral ring buffer, drained later by whoever asked to read. The ISR drains the hardware FIFO in a loop, because one interrupt can stand for several queued bytes. |
| **TTY line discipline & `/dev/console`** | Canonical mode — bytes accumulate into a line, each echoes as it arrives, backspace both removes the byte *and* emits `\b \b` so the character visually disappears, Enter delivers the line with its newline. Exposed as a real `VFS_CHARDEVICE` node mounted at `/dev/console`, not a special case in the syscall dispatcher: every task is born with fds `0`/`1`/`2` already open onto it, so `write(1, …)` is an ordinary validated path through the fd table, and `lseek` on it honestly returns `-ESPIPE`. With no wait queue yet, `read()` spins rather than sleeps — a named stopgap Phase 7 replaces, sound today only because enabling interrupts across syscalls made the spin preemptible. |
| **Synchronization** | `arch_irq_save()`/`arch_irq_restore()` save and *restore* interrupt state rather than unconditionally enabling it, and `struct spinlock` sits on top with an irqsave-only API. Deliberately no plain `spin_lock()`: on one core, a task holding a lock the UART ISR wants deadlocks outright, so masking-before-acquiring is enforced by the API instead of by comment. |
| **Framebuffer terminal** | Direct 32-bit pixel drawing plus an 8x16 bitmap font renderer with line wrapping and scrolling. UEFI leaves no hardware text mode behind, so the terminal is drawn by hand, pixel by pixel. |
| **Logging** | `kprintf(tag, ...)` stamps every line with a padded `[SUBSYS]` prefix, keeping the boot log aligned in one column and greppable by subsystem. `kpanic` reports and halts. |
| **Self-tests** | One file per subsystem under `core/test/`, gated behind a `pros.tests` kernel command line so a normal boot stays quiet. Each check prints a single `PASS`/`FAIL` line — 116 of them today, across PMM, heap, VMM, VFS, kernel stacks, tasks, the input ring buffer and the line discipline, including the sparse-file and page-boundary paths the initrd loader can never reach on its own, and the backspace/overflow edges of the line editor that no amount of typing reaches reliably. |
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
├── doc/                   # Roadmap, standing references, and working docs for phases being designed or built
│   └── archive/           # Finished & superseded working documents, kept for the reasoning trail
├── kernel/                # Kernel source root
│   ├── Makefile           # ARCH-parametrized build → kernel/bin/$(ARCH)/kernel
│   ├── config.x86_64.mk, config.aarch64.mk   # Per-arch CC/CFLAGS/LDFLAGS
│   ├── include/           # Kernel headers (arch, asm, core, fs, mm, proc, syscall)
│   └── src/               # Kernel sources
│       ├── arch/          # Architecture-specific code (aarch64, x86_64), linker scripts, exceptions, interrupts
│       ├── core/          # Core kernel subsystems (boot, fb, console, console_input, ldisc, ring_buffer, spinlock, kprintf, main) and test/ (one self-test file per subsystem)
│       ├── drivers/       # Device nodes that speak VFS (console_dev — /dev/console)
│       ├── fs/            # Filesystems: vfs/ (core + fd table), ramfs/ (in-memory root), tar/ (initrd loader)
│       ├── mm/            # Memory Management (pmm, vmm, heap)
│       ├── proc/          # Tasks & scheduling (task, sched, kstack)
│       └── syscall/       # Syscall dispatch table
├── user/                  # Userland programs, one subdirectory per program
│   ├── Makefile           # ARCH-parametrized, auto-discovers src/*/ → user/bin/$(ARCH)/<program>
│   ├── config.x86_64.mk, config.aarch64.mk   # Per-arch CC/LDFLAGS (freestanding, linux-none target)
│   ├── include/           # Shared syscall wrappers every program #includes — no libc
│   └── src/init/          # /bin/init — opens, reads, writes, closes, exits, for real
└── initrd/                # The RAM-backed root filesystem's tracked source content
    ├── Makefile           # Packs data/ + that arch's user/ binaries into initrd/bin/$(ARCH)/initrd
    └── data/root/         # Tracked filesystem content (e.g. hello.txt) — the name IS the runtime /root path
```

`kernel/libs/` (downloaded third-party libraries — freestanding headers, printf, limine) is
gitignored build output, same as every subproject's `bin/`.

---

## 🗺️ Development Roadmap & Status

Development happens in iterative phases, each sized to end in something demonstrable rather than
merely correct. **[`doc/ROADMAP.md`](doc/ROADMAP.md) is the source of truth** for the phase
breakdown, status, and payoff of everything — done, in progress, and planned — not this README.

Phases 1 through 5 are complete on both architectures, and Phase 6 is two Steps in. Most
recently: **PrOS talks back.** Type a line over `-serial stdio` — backspacing over your typos —
and `/bin/init` reads it through `/dev/console` as an ordinary file descriptor and prints it
back. The whole path is real: hardware interrupt → byte queue → line discipline → VFS node →
syscall → an unprivileged program. Each phase gets its own working document while it's being
designed or built, then moves to [`doc/archive/`](doc/archive/) once it closes, kept for the
reasoning trail rather than deleted.

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

Builds both architectures and boots each headless, back to back — downloads OVMF/Limine on first
run, then launches QEMU:

```bash
make
```

Run one architecture at a time, with a graphical window:

```bash
make qemu-x86_64
make qemu-aarch64
```

Or headless (serial output only, also saved to `logs/`):

```bash
make qemu-x86_64-nographic
make qemu-aarch64-nographic
```

The kernel shuts itself down at the end of boot, so QEMU exits on its own — but `/bin/init` now
waits for you to type a line first, so give it one. To bail out early: `Ctrl+A` then `X`.

```bash
make clean       # remove build output
make distclean   # also remove downloaded OVMF/Limine/libs
```


