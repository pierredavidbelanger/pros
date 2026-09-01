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
| **Boot** | One kernel source tree for AArch64 (`virt`) and x86_64 (`q35`), booted by Limine v6 over OVMF. CPU differences sit behind an `arch_*` interface rather than `#ifdef`s scattered through shared code, and the bootloader is left to solve bootloader problems — framebuffer, HHDM, memory map, cmdline, initrd, DTB/RSDP all arrive as responses. |
| **Physical memory (PMM)** | 4 KiB page-frame allocator over a free list built from Limine's memory map, with contiguous multi-page allocation and physical↔virtual translation through the higher-half direct map. |
| **Virtual memory (VMM)** | Architecture-neutral 4-level page-table walker, with the genuine hardware split (x86_64's single `CR3` vs AArch64's `TTBR0`/`TTBR1` pair) isolated behind `arch_vmm_*`. Demand paging decodes the *actual* fault reason instead of assuming every fault is legitimate. |
| **Kernel heap** | First-fit free list with block splitting, coalescing on free, and a `krealloc` that grows in place by absorbing a free neighbour. Alignment is guaranteed on the returned pointer, not merely on the requested size. |
| **Exceptions, interrupts & timer** | IDT with assembly stubs on x86_64, EL1 vector table on AArch64; 8259 PIC + 8254 PIT with the LAPIC in virtual wire mode, or GICv2 + the ARM Generic Timer. Both feed one architecture-neutral 100 Hz tick — the hardware differs, "how many times has it fired" does not. Anything genuinely unhandled dumps full register state *and every task's* before halting, so a crash is diagnosable from the boot log alone. |
| **Tasks & preemptive scheduler** | Round-robin over a circular run queue, driven by the timer and by tasks that block. A **dedicated scheduler thread** — `BOOT`, living outside the run queue — is the only thing that dispatches: a task hands the CPU back through `switch_to`, which saves the callee-saved set and the kernel stack pointer, and is resumed later on its own stack in the middle of whatever call it stopped in. Deciding to switch and actually switching stay deliberately separate, so the kernel is *preempted at trap exit* rather than preemptible. A task that has never run owns two fabricated frames rather than one: a trap frame built to look like one the hardware would have written, and a switch frame whose return address is a trampoline that takes it out through the normal trap-return tail. `Ctrl+P` dumps every task and its switch count, xv6-style, which is the only way to watch a scheduler that has correctly stopped scheduling. |
| **Kernel stacks** | One 16 KiB stack per task with a guard value at the base, so an overflow panics instead of quietly eating a neighbour. Per-task rather than one global exception stack, because sharing corrupts memory in proportion to how *well* the scheduler is working. |
| **Synchronization** | `arch_irq_save()`/`arch_irq_restore()` restore interrupt state rather than unconditionally enabling it, with `struct spinlock` on top. The `_irqsave` pair is the composition of the two halves and the one every caller should take: on one core, a task holding a lock the UART ISR wants deadlocks outright, so masking-before-acquiring is the rule the API is shaped around. The bare `spinlock_lock`/`spinlock_unlock` exist for exactly one caller — `sleep()`, which must drop the lock and *stay* masked until it is off the CPU, because restoring the mask any earlier reopens the lost-wakeup window it exists to close. |
| **VFS, ramfs & initrd** | Mount table with longest-prefix matching, a `vfs_ops` vtable, one shared path resolver, and a per-process fd table under Linux-shaped syscalls. `ramfs` owns storage and is mounted at `/`; `tar_load()` is *only* a ustar parser filling it through the public VFS API, so the archive format never touches filesystem internals. Files are read/write over a sparse page list, so seeking past the end and writing produces a real hole. |
| **Console & TTY** | Output fans out to serial (live from the first line of boot) and to the framebuffer terminal once it exists. Input comes the other way: the UART RX interrupt feeds a canonical-mode line discipline directly, turning bytes into editable lines — backspace emits `\b \b` so the character actually disappears. Exposed as a real `VFS_CHARDEVICE` at `/dev/console`, not a special case in the dispatcher: every task is born with fds `0`/`1`/`2` open onto it. A `read()` that finds no finished line **sleeps** on the line discipline as its channel and leaves the run queue entirely; the ISR wakes it when a line completes, and a machine with nothing to do sits in `wfi`/`hlt` rather than spinning through a loop whose job is to notice that nothing happened. |
| **`psh` — a shell you can type at** | A freestanding shell, no libc: prompt, read a line, split on spaces, run a builtin — `help`, `echo`, `cat`, `ls`, `exit`. Builtins only, because `fork`/`exec` don't exist yet. `ls` walks packed `struct linux_dirent64` entries by `d_reclen` from ring 3, which is the Linux ABI having a *layout* rather than just a number for the first time. Which program `init` is comes from `pros.init=`, and the machine shuts down when the last task dies rather than when a timer expires. |
| **Framebuffer terminal & logging** | Direct 32-bit pixel drawing plus an 8x16 bitmap font with wrapping and scrolling, because UEFI leaves no hardware text mode behind. `kprintf(tag, …)` stamps every line with a padded `[SUBSYS]` prefix, so the boot log stays aligned in one column and greppable by subsystem. |
| **Self-tests** | One file per subsystem under `core/test/`, gated behind `pros.tests` so a normal boot stays quiet. 130 `PASS`/`FAIL` lines today — including the sparse-file and page-boundary paths the initrd loader can never reach on its own, and the backspace/overflow edges of the line editor that no amount of typing reaches reliably. |
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
│       ├── core/          # Core kernel subsystems (boot, fb, console, ldisc, ring_buffer, spinlock, kprintf, main) and test/ (one self-test file per subsystem)
│       ├── drivers/       # Device nodes that speak VFS (console_dev — /dev/console)
│       ├── fs/            # Filesystems: vfs/ (core + fd table), ramfs/ (in-memory root), tar/ (initrd loader)
│       ├── mm/            # Memory Management (pmm, vmm, heap)
│       ├── proc/          # Tasks & scheduling (task, sched, kstack)
│       └── syscall/       # Syscall dispatch table
├── user/                  # Userland programs, one subdirectory per program
│   ├── Makefile           # ARCH-parametrized, auto-discovers src/*/ → user/bin/$(ARCH)/<program>
│   ├── config.x86_64.mk, config.aarch64.mk   # Per-arch CC/LDFLAGS (freestanding, linux-none target)
│   ├── include/           # Shared syscall wrappers, string helpers, ABI structs — no libc
│   ├── src/init/          # /bin/init — opens, reads, writes, closes, exits, for real
│   └── src/psh/           # /bin/psh — the shell: prompt, builtins, exit
├── initrd/                # The RAM-backed root filesystem's tracked source content
│   ├── Makefile           # Packs data/ + that arch's user/ binaries into initrd/bin/$(ARCH)/initrd
│   └── data/root/         # Tracked filesystem content (hello.txt, lorem.txt, empty.txt) — the name IS the runtime /root path
└── tools/zig-cc-clang     # The CC every config.$(ARCH).mk points at — one exe wrapping `zig cc`
```

`kernel/libs/` (downloaded third-party libraries — freestanding headers, printf, limine) is
gitignored build output, same as every subproject's `bin/`.

---

## 🗺️ Development Roadmap & Status

Development happens in iterative phases, each sized to end in something demonstrable rather than
merely correct. **[`doc/ROADMAP.md`](doc/ROADMAP.md) is the source of truth** for the phase
breakdown, status, and payoff of everything — done, in progress, and planned — not this README.

Phases 1 through 6 are complete on both architectures. Most recently: **PrOS answers back.** It
boots to a `#` prompt over `-serial stdio`, and you use it — `ls /root`, `cat /root/hello.txt`,
`echo`, backspacing over your typos — until you type `exit`, at which point the machine shuts down
because the last program ended, not because a timer ran out. The whole path is real: hardware
interrupt → byte queue → line discipline → VFS node → syscall → an unprivileged shell you wrote.
Each phase gets its own working document while it's being designed or built, then moves to
[`doc/archive/`](doc/archive/) once it closes, kept for the reasoning trail rather than deleted.

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

Boot lands you at a `#` prompt — try `help`, then `ls /root` and `cat /root/hello.txt`. The
machine shuts down, and QEMU exits on its own, once you type `exit`. To bail out early:
`Ctrl+A` then `X`.

```bash
make clean       # remove build output
make distclean   # also remove downloaded OVMF/Limine/libs
```


