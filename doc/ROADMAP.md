# PrOS Development Roadmap & Architectural Milestones

This document outlines the step-by-step development roadmap for **PrOS** (PjErOS), progressing from the current kernel infrastructure to a userland graphics environment running **BusyBox**, **X11 (Xfbdev)**, **lwm**, **xterm**, and a web browser like **lynx** or **elinks**.

---

## 🎯 Core Strategy: Linux System Call ABI

To bring up standard Unix software (BusyBox, X11 server, window managers, terminals, and browsers) without porting each application individually, PrOS adopts the **Linux System Call ABI** (standard Linux syscall numbers and argument conventions for `x86_64` and `AArch64`).

Pairing the kernel's Linux ABI syscall dispatcher with a standard C library allows off-the-shelf Linux software to compile and run smoothly on PrOS.

---

## 📋 Detailed Milestone Breakdown

### Phase 1: Virtual Memory Management (VMM) & Demand Paging
* **Status**: **COMPLETE** ✅ — later revisited for an architecture-neutral redesign pass: single source of truth for the paging-level/index constants (was hand-copied in three functions), a real per-context demand-paging policy that inspects the actual fault reason (`arch_vmm_fault_is_present`/`arch_vmm_fault_is_write`) instead of ignoring it, a fixed memory leak in context teardown (previously only freed the root table, not the tree beneath it), and root-table setup/cloning delegated to per-architecture `arch_vmm_ensure_user_root()`/`arch_vmm_new_context_root()` so `vmm.c` no longer needs to explain x86_64-vs-AArch64 differences in its own comments. The working docs for both passes (`PHASE1_VMM.md`, `PHASE1_VMM_REDESIGN.md`) have been folded into this summary and removed now that the work is done.
* **Prerequisites**: Physical Memory Manager (PMM) — implemented in `kernel/mm/pmm.c`.
* **Goal**: Build page-table abstractions to isolate kernel memory space from user processes and support dynamic memory mapping.

---

### Phase 2: In-Memory Root Filesystem (Initramfs) & VFS
* **Status**: **Directory tree loads end-to-end** ✅ — **file content storage still open** 🚧 (Detailed working doc: [`PHASE2_RAMFS.md`](PHASE2_RAMFS.md), which supersedes [`PHASE2_TAR_DRIVER.md`](PHASE2_TAR_DRIVER.md))
* **Goal**: Mount a real root filesystem with no disk driver at all, by reading a `ustar` archive Limine already loads into RAM as a boot module.
* **History**: The original plan targeted a disk-backed FAT filesystem over a hand-written VirtIO-block/PCI/ACPI stack (`PHASE2_VFS.md` Steps 1-9) — it worked, but wasn't clean enough to keep, and was fully removed (`git log`: *"rip out everything i found not clean enough, will redo later"*) in favor of the simpler initrd approach below. The VFS core (`fs/vfs/`) and its syscalls were untouched by the rip-out and remain exactly as originally built.
* **Key Tasks**:
  1. ✅ **Iterative VFS path resolution** (`vfs_lookup`) — `sys_open`'s old single-segment shortcut replaced with real `/`-delimited path tokenization.
  2. ✅ **Limine module parsing** — `module_request` registered in `boot.c`, `limine.conf` carries the module directive, `initrd.tar` built and staged by the Makefile.
  3. ✅ **ramfs driver + TAR loader split** (`fs/ramfs/ramfs.c`, `fs/tar/tar.c`) — the archive format no longer *is* the filesystem. `ramfs` owns the storage (in-memory tree, first-child/next-sibling), and `tar_load()` is reduced to a parser that populates it purely through the public VFS path API — the same separation as Linux's `rootfs` + `unpack_to_rootfs()`. Originally `tar_mount()` did both jobs at once; making that writable would have made it a tmpfs wearing a tar costume, hence the split.
  4. ✅ **Mount to VFS root `/`** — `ramfs_create_root()` mounted at `/` before anything else, then `tar_load()` fills it. Verified at boot: `/root` resolves through the mount table and `readdir` lists `hello.txt`.
  5. 🚧 **File content storage** — `ramfs_ops_read`/`ramfs_ops_write` are still stubs, so `cat` produces nothing yet. Design in [`PHASE2_RAMFS.md`](PHASE2_RAMFS.md) Part 2: a sparse page list per node (`void **pages`, `NULL` entry = hole reading as zeros), which is what real `tmpfs` does and what `mmap` will need in Phase 5.
  6. ✅ **Supporting work that fell out of the split** — `vfs_ops.create` plus `vfs_create()`/`vfs_mkdir_parents()` path helpers; `krealloc()` in `heap.c` (in-place growth via neighbour coalescing) for the page-array growth; heap payload alignment fixed (`HEAP_HEADER_SIZE`); `pmm_alloc()` now returns 0 instead of panicking, with every caller checking; a self-test suite under `core/test/` (18 checks) and a tagged `kprintf(tag, ...)`.

---

### Phase 3: Kernel Preemption, Architecture Stacks, Syscalls & Init Process
* **Goal**: Launch and run the first userland process (`/bin/init`).
* **Key Tasks**:
  1. **Task State Segment (TSS) & Kernel Stack Switch**:
     - **x86_64**: Setup TSS with a valid `rsp0` for user-to-kernel stack switching on interrupts/syscalls (prevents Double Faults).
     - **AArch64**: Manage switching between `SP_EL0` (user stack) and `SP_EL1` (kernel stack) in exception handlers.
  2. **FPU / SIMD Context Switching**:
     - Enable `CR4.OSFXSR` / `CR4.OSXSAVE` (x86_64) or `CPACR_EL1` FP instructions (AArch64).
     - Save/restore vector register state (`fxsave`/`fxrstor` or `XSAVE`) in context switch routines to prevent corruption across tasks.
  3. **Monotonic Clocks & Timers**:
     - Configure APIC/PIT (x86_64) or Generic Timer (AArch64).
     - Implement kernel millisecond tick counter to back time syscalls (`sys_clock_gettime`, `sys_nanosleep`).
  4. **Process Control Block (PCB/TCB) & Preemptive Scheduler**:
     - Track PID, process state, user/kernel stacks, open file descriptors, and CPU register state.
     - Round-robin preemptive task scheduler (`switch_to`).
  5. **Userland Context Switch**:
     - Ring 0 $\rightarrow$ Ring 3 transition (`iretq` on x86_64).
     - EL1 $\rightarrow$ EL0 transition (`eret` on AArch64).
  6. **ELF Executable Loader**:
     - Parse ELF64 binaries, map `PT_LOAD` segments into user page directory, setup initial user stack (`argc`, `argv`, `envp`, `auxv`).
  7. **Linux Syscall Interface**:
     - Implement `syscall`/`sysret` (x86_64) and `svc` (AArch64) handlers.
     - Dispatch standard syscalls: `sys_read`, `sys_write`, `sys_open`, `sys_close`, `sys_execve`, `sys_fork`/`sys_clone`, `sys_exit`, `sys_brk`/`sys_mmap`, `sys_waitpid`.
  8. **Boot `/bin/init`**: Execute `/bin/init` as PID 1 upon kernel initialization.

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
