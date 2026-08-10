# PrOS Development Roadmap & Architectural Milestones

This document outlines the step-by-step development roadmap for **PrOS** (PjErOS), progressing from the current kernel infrastructure to a userland graphics environment running **BusyBox**, **X11 (Xfbdev)**, **lwm**, **xterm**, and a web browser like **lynx** or **elinks**.

---

## 🎯 Core Strategy: Linux System Call ABI

To bring up standard Unix software (BusyBox, X11 server, window managers, terminals, and browsers) without porting each application individually, PrOS adopts the **Linux System Call ABI** (standard Linux syscall numbers and argument conventions for `x86_64` and `AArch64`).

Pairing the kernel's Linux ABI syscall dispatcher with a standard C library allows off-the-shelf Linux software to compile and run smoothly on PrOS.

---

## 📂 How `doc/` is organized

- **`doc/`** holds what is *live*: this roadmap, the working document for the phase currently
  being designed or built, [`SYSCALL_DESIGN.md`](SYSCALL_DESIGN.md) as a standing reference,
  and [`SIDEQUEST.md`](SIDEQUEST.md).
- **[`doc/archive/`](archive/)** holds finished and superseded working documents. They are kept
  for the reasoning trail — *why* a design was chosen or abandoned is worth more than the
  design itself — but they describe code that has either shipped or never existed.

> [!IMPORTANT]
> When designing something new, read `doc/` and skip `doc/archive/`. Each phase's outcome is
> summarized in its Status line below, so the archived documents shouldn't need consulting
> unless the question is specifically "why was it done this way?". This applies to Claude too.

---

## 📋 Detailed Milestone Breakdown

### Phase 1: Virtual Memory Management (VMM) & Demand Paging
* **Status**: **COMPLETE** ✅ — later revisited for an architecture-neutral redesign pass: single source of truth for the paging-level/index constants (was hand-copied in three functions), a real per-context demand-paging policy that inspects the actual fault reason (`arch_vmm_fault_is_present`/`arch_vmm_fault_is_write`) instead of ignoring it, a fixed memory leak in context teardown (previously only freed the root table, not the tree beneath it), and root-table setup/cloning delegated to per-architecture `arch_vmm_ensure_user_root()`/`arch_vmm_new_context_root()` so `vmm.c` no longer needs to explain x86_64-vs-AArch64 differences in its own comments. The working docs for both passes (`PHASE1_VMM.md`, `PHASE1_VMM_REDESIGN.md`) have been folded into this summary and removed now that the work is done.
* **Prerequisites**: Physical Memory Manager (PMM) — implemented in `kernel/mm/pmm.c`.
* **Goal**: Build page-table abstractions to isolate kernel memory space from user processes and support dynamic memory mapping.

---

### Phase 2: In-Memory Root Filesystem (Initramfs) & VFS
* **Status**: **COMPLETE** ✅ — a read/write in-memory root filesystem, populated at boot from the initrd, verified end-to-end by 31 VFS self-tests (Detailed working doc: [`archive/PHASE2_RAMFS.md`](archive/PHASE2_RAMFS.md), which supersedes [`archive/PHASE2_TAR_DRIVER.md`](archive/PHASE2_TAR_DRIVER.md)). Deliberately left for later: `truncate`, `unlink`, and `O_CREAT`/`O_TRUNC` in `sys_open` — none are needed until something actually deletes or replaces a file.
* **Goal**: Mount a real root filesystem with no disk driver at all, by reading a `ustar` archive Limine already loads into RAM as a boot module.
* **History**: The original plan targeted a disk-backed FAT filesystem over a hand-written VirtIO-block/PCI/ACPI stack ([`archive/PHASE2_VFS.md`](archive/PHASE2_VFS.md) Steps 1-9) — it worked, but wasn't clean enough to keep, and was fully removed (`git log`: *"rip out everything i found not clean enough, will redo later"*) in favor of the simpler initrd approach below. The VFS core (`fs/vfs/`) and its syscalls were untouched by the rip-out and remain exactly as originally built.
* **Key Tasks**:
  1. ✅ **Iterative VFS path resolution** (`vfs_lookup`) — `sys_open`'s old single-segment shortcut replaced with real `/`-delimited path tokenization.
  2. ✅ **Limine module parsing** — `module_request` registered in `boot.c`, `limine.conf` carries the module directive, `initrd.tar` built and staged by the Makefile.
  3. ✅ **ramfs driver + TAR loader split** (`fs/ramfs/ramfs.c`, `fs/tar/tar.c`) — the archive format no longer *is* the filesystem. `ramfs` owns the storage (in-memory tree, first-child/next-sibling), and `tar_load()` is reduced to a parser that populates it purely through the public VFS path API — the same separation as Linux's `rootfs` + `unpack_to_rootfs()`. Originally `tar_mount()` did both jobs at once; making that writable would have made it a tmpfs wearing a tar costume, hence the split.
  4. ✅ **Mount to VFS root `/`** — `ramfs_create_root()` mounted at `/` before anything else, then `tar_load()` fills it. Verified at boot: `/root` resolves through the mount table and `readdir` lists `hello.txt`.
  5. ✅ **File content storage** — each node carries a sparse page list (`void **pages`, a `NULL` entry being a hole that reads as zeros), grown on demand via `krealloc` and backed page-by-page by the PMM. That is what real `tmpfs` does and what `mmap` will need in Phase 5. `lseek` past EOF then write produces a genuine hole, with no zero-fill pass and no pages allocated for the gap.
  6. ✅ **Supporting work that fell out of the split** — `vfs_ops.create` plus `vfs_create()`/`vfs_mkdir_parents()` path helpers; `krealloc()` in `heap.c` (in-place growth via neighbour coalescing) for the page-array growth; heap payload alignment fixed (`HEAP_HEADER_SIZE`); `pmm_alloc()` now returns 0 instead of panicking, with every caller checking; a self-test suite under `core/test/` (64 checks) and a tagged `kprintf(tag, ...)`.

---

### Phase 3: Kernel Preemption, Architecture Stacks, Syscalls & Init Process
* **Status**: **NOT STARTED** ⬜ — designed but unwritten. Working doc:
  [`PHASE3_USERLAND.md`](PHASE3_USERLAND.md), which carries the concept-by-concept
  explanations, the per-architecture differences, the decisions taken, and the list of things
  already in the tree that are latently wrong once a lower privilege level starts trapping.
* **Goal**: Launch and run the first userland process (`/bin/init`).
* **Starting line**: further along than it looks. The full trap frame exists on both
  architectures, a TSS is built and loaded, `vmm_create_context()`/`vmm_switch_context()`
  already give a process its own address space, and `VMM_USER` is plumbed to hardware. What's
  missing is a timer, per-task kernel stacks, the privilege drop, and the syscall entry path.
* **Key Tasks**: ordered so that each step boots green on its own, and so "interruption" is
  working (and debuggable, in ring 0) before "distrust" is introduced. Preemption is proven
  with *kernel* threads before any userland exists, because a scheduler bug and a privilege
  transition bug both present as a machine reset.
  1. ⬜ **Timer & interrupts, no scheduler** — IDT gates above vector 31 + PIC remap + PIT
     (x86_64); GIC + Generic Timer (AArch64, the larger half). A monotonic tick counter to
     back `sys_clock_gettime`/`sys_nanosleep` later. Broken into baby steps in
     [`PHASE3_STEP1_TIMER.md`](PHASE3_STEP1_TIMER.md).
  2. ⬜ **`struct task`, per-task kernel stacks, trap-stub contract change** — the C handler
     returns the frame to restore instead of `void`, which is what makes a context switch four
     lines of assembly. Includes `tss.rsp0` (x86_64) / `SP_EL1` (AArch64) maintenance and the
     `vectors.S` correctness fixes. Still one task, so nothing observable changes.
  3. ⬜ **Two kernel threads, round-robin** — the milestone where preemption is real, entirely
     in ring 0.
  4. ⬜ **Ring 3 / EL0 transition** — `iretq` (x86_64) / `eret` (AArch64) into a hand-fabricated
     user program, before the ELF loader exists. Needs user segments in the GDT, laid out to
     `sysret`'s constraint even though step 5 is what enforces it.
  5. ⬜ **Syscall entry path** — `svc` dispatch (AArch64, small: it lands in the existing vector
     table); `syscall`/`sysret` + `swapgs` + the `STAR`/`LSTAR`/`FMASK` MSRs (x86_64, not
     small). One syscall wired: `write`.
  6. ⬜ **ELF64 loader & a real `/bin/init`** — `PT_LOAD` segments mapped into the new context,
     `.bss` zeroed, initial user stack built (`argc`, `argv`, `envp`, `auxv`). A freestanding
     `init.c` built with the existing `zig cc` toolchain, no libc needed yet. **Phase 3's
     stated goal is met here.**
  7. ⬜ **The real syscall surface** — per-process fd tables (they're kernel-global today),
     `copy_from_user` validation, `-errno` returns replacing `-1`, and
     `open`/`read`/`close`/`exit`/`getpid` reachable from ring 3.
  8. ⬜ **`fork`, `execve`, `wait4` & voluntary switching** — the genuinely hard step, and where
     a real `switch_to` becomes unavoidable (blocking in `wait4` is not a timer-driven
     switch). Likely earns its own working document.
  * ⬜ **FPU / SIMD context switching** slots in wherever a second *user* task first exists.
    AArch64 is nearly free (`-mgeneral-regs-only` means the kernel emits no FP at all); the
    x86_64 kernel currently *does* use SSE, so it needs `-mno-sse` to make lazy switching
    correct rather than approximately correct.

---

### Phase 4: C Library Integration & Framebuffer/UART BusyBox Shell
* **Goal**: Run an interactive BusyBox shell on framebuffer/UART terminal.
* **Key Tasks**:
  1. **C Library Integration (`mlibc` / `Newlib`)**:
     - Port or configure the C library to map system calls to the kernel's Linux ABI dispatcher.
  2. **Keyboard Driver & TTY Subsystem**:
     - Implement PS/2 or VirtIO keyboard driver.
     - Implement TTY driver and line discipline (canonical/raw modes, echoing, backspace) attached to `/dev/tty1` (framebuffer) and `/dev/ttyS0` (UART).
  3. **POSIX Signal Infrastructure**:
     - Implement `sigaction`, `sigprocmask`, signal delivery frames on the user stack, and `sys_sigreturn`.
     - Essential for BusyBox job control (`Ctrl+C`, backgrounding `&`, `SIGCHLD`).
  4. **Cross-Compile BusyBox**: Build BusyBox against the target C library.
  5. **Spawn Shell**: Init spawns `/bin/sh` from BusyBox on `/dev/tty1` and `/dev/ttyS0`.

---

### Phase 5: Framebuffer Device (`/dev/fb0`), Inputs & Pseudo-Terminals (PTYs)
* **Goal**: Prepare device infrastructure for X11 graphic software.
* **Key Tasks**:
  1. **`/dev/fb0` Device Node**: Expose Limine framebuffer through `/dev/fb0` with `mmap` support and `ioctl` handling (`FBIOGET_VSCREENINFO`, `FBIOGET_FSCREENINFO`).
  2. **Mouse & Input Drivers**: PS/2 mouse or VirtIO input driver exposing `/dev/input/event0` or `/dev/mouse`.
  3. **Pseudo-Terminal (PTY) Subsystem (`/dev/ptmx` & `/dev/pts/*`)**:
     - Master/slave PTY driver with termios `ioctl` support (`TCGETS`, `TCSETS`, `TIOCGWINSZ`, `TIOCSCTTY`).
     - Mandatory requirement for launching `xterm` subshells.

---

### Phase 6: Minimal X Server (`Xfbdev`), Window Manager (`lwm`) & `xterm`
* **Goal**: Launch an X11 graphical windowing environment with a window manager and terminal emulator.
* **Key Tasks**:
  1. **UNIX Domain Sockets (`AF_UNIX`)**: Implement local IPC sockets (`socket`, `bind`, `connect`, `listen`, `accept`, `send`, `recv`) for `/tmp/.X11-unix/X0`.
  2. **Signals & Pipes**: Implement `pipe`, `dup2`, `sigaction`, and `kill`.
  3. **Compile `Xfbdev`**: Build TinyX / `Xfbdev` against the C library to render directly to `/dev/fb0`.
  4. **Compile `lwm` & `xterm`**:
     - Cross-compile X11 client libraries (`libX11`, `libXext`, `libXt`, `libXaw`).
     - Build `lwm` (Light Window Manager) to run on top of `Xfbdev`.
     - Build `xterm` to run inside `lwm`.

---

### Phase 7: VirtIO Networking & Web Browser (`lynx` / `elinks`)
* **Goal**: Connect to the network and browse the web from inside PrOS.
* **Key Tasks**:
  1. **VirtIO-Net Driver**: Implement PCI/MMIO packet transmit and receive driver.
  2. **TCP/IP Stack**: Integrate lwIP or internal network stack exposing BSD socket system calls (`sys_socket`, `sys_bind`, `sys_connect`, `sys_send`, `sys_recv`).
  3. **Compile Lynx / ELinks**: Cross-compile console web browser with HTTP/DNS support to run inside `xterm` or on the framebuffer shell.
