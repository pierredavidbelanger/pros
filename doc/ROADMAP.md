# PrOS Development Roadmap & Architectural Milestones

This document outlines the step-by-step development roadmap for **PrOS** (PjErOS), progressing from the current kernel infrastructure to a userland graphics environment running **BusyBox**, **X11 (Xfbdev)**, **lwm**, **xterm**, and a web browser like **lynx** or **elinks**.

---

## 🎯 Core Strategy: Linux System Call ABI

To bring up standard Unix software (BusyBox, X11 server, window managers, terminals, and browsers) without porting each application individually, PrOS adopts the **Linux System Call ABI** (standard Linux syscall numbers and argument conventions for `x86_64` and `AArch64`).

Pairing the kernel's Linux ABI syscall dispatcher with a standard C library allows off-the-shelf Linux software to compile and run smoothly on PrOS.

---

## 📂 How `doc/` is organized

- **`doc/`** holds what is *live*: this roadmap, the working documents for the phases currently
  being designed or built, [`SYSCALL_DESIGN.md`](SYSCALL_DESIGN.md) as a standing reference,
  and [`SIDEQUEST.md`](SIDEQUEST.md). More than one phase document can be live at a time — a
  phase can be fully designed long before it's started, which is where Phases 4 and 5 sit.
- **[`doc/archive/`](archive/)** holds finished and superseded working documents. They are kept
  for the reasoning trail — *why* a design was chosen or abandoned is worth more than the
  design itself — but they describe code that has either shipped or never existed.

> [!IMPORTANT]
> When designing something new, read `doc/` and skip `doc/archive/`. Each phase's outcome is
> summarized in its Status line below, so the archived documents shouldn't need consulting
> unless the question is specifically "why was it done this way?". This applies to Claude too.

### Conventions

Four words, each meaning exactly one thing. They accumulated informally and collided — "Step"
meant one level in this file and another in the phase documents, "Part" meant both a chapter you
read and an item you do. This is the settled version. **It applies to Claude too.**

| Level | Word | Label form | Lives in |
|---|---|---|---|
| 1 | **Phase** | `Phase 3` | this file, and nowhere else |
| 2 | **Step** | `Phase 3 Step 2` | this file's list; gets its own document when it starts |
| 3 | **Part** | `Part C0`, `Part B1` | a step document |
| — | **Chapter** | `Chapter 6` | a phase document |

```
ROADMAP.md                               Phase 3
  └─ PHASE3_PREEMPTION.md                Chapters 0-8   (read these)
     │                                   Chapter 6 lists the Steps (do these)
     └─ PHASE3_STEP2_KERNEL_STACKS.md    Parts  C0 C1 · A1 · B1 B2 · C2 C3 C4
```

- **Only this file creates Phases.** Nothing else numbers them.
- **"Step" is the only word for a unit of work inside a phase**, numbered sequentially within
  that phase and never reset — including across a redesign. Phase 2 ran Steps 1-14 across
  three documents; `archive/PHASE2_STEP14_RAMFS.md` is Step 14 even though Steps 1-9 were
  later ripped out.
- **"Chapter" never means work.** Phase documents have Chapters; step documents never do.
- **Part letters name the track, not the order:** `C` architecture-neutral, `A` x86_64,
  `B` AArch64, `D`+ any further track, declared at the top of that document. Which track to do
  first is the order diagram's job, so a step that recommends ARM-first renumbers nothing.
- **Part `0` is reserved** for setup that precedes both architecture tracks. Everything else
  starts at 1.
- **Every Phase, Step and Part carries a status marker** — ⬜ / 🚧 / ✅. On a Phase it lives in
  the `**Status**:` bullet rather than the heading, because that bullet carries the prose
  summary too and the two belong together.
- **Headings carry the bare label** (`## C0 — One name for the frame struct`); the word "Part"
  appears in prose and cross-references (`see Part B2`). Chapters *do* carry the word, because
  `## 6: The syscall entry path` doesn't read as anything.
- **Full reference form** is `Phase 3 Step 2 Part B1`. Shorten freely when context makes it
  unambiguous.
- **Retired words:** "bite", and **"task" as a structural word** — Phase 3 introduces
  `struct task`, and "Key Task 2 builds the task struct" is a sentence nobody should have to
  read. "Baby steps" survives as informal prose for what a step document contains; it is never
  a label.
- **No numbered list markers in this file** — plain `-` bullets throughout. A Step already
  carries its number in the item text (`**Step 2 — …**`), so an ordered marker would only
  duplicate it, and nested ordered lists render badly in some editors (CLion among them).

**Document names:** `PHASE<N>_<TOPIC>.md` for a phase document, `PHASE<N>_STEP<M>_<TOPIC>.md`
for a step document, both moving to `archive/` on completion. The `<TOPIC>` must not be a word
this convention has claimed — a file called `PHASE3_STEP2_TASKS.md` reads as "Step 2's tasks"
rather than "the step about `struct task`", which is exactly the ambiguity the retired-words
rule exists to kill. A phase document may list its Steps inline instead of giving each one its
own file — `archive/PHASE2_VFS.md` did exactly that for Steps 1-13, and that stays legal.

Everything else in `doc/` is neither a phase nor a step document and keeps a plain topic name:
this file, [`SYSCALL_DESIGN.md`](SYSCALL_DESIGN.md) (a standing reference that outlives any one
phase) and [`SIDEQUEST.md`](SIDEQUEST.md) (detours that aren't on the phase list at all).

### The phase-list lifecycle

Each phase below carries a list, and the list changes meaning as the phase matures — it always
has. Naming the lifecycle beats pretending the result is drift. **The heading is a function of
two yes/no facts** — is the phase complete, and does it have a working document — so there is
no judgement call:

| State | Heading | What the list is | How much to trust it |
|---|---|---|---|
| ✅ complete | **What shipped** | a thematic retrospective of what survives | descriptive; **not** numbered as Steps |
| has a working document | **Steps** | the ordered plan; item *n* **is** Step *n* | authoritative; mirrors that document |
| no working document | **Planned scope** | a wish list for an undesigned phase | none — not a commitment |

Note the middle row keys off the *document*, not the ⬜/🚧 marker: a phase that is designed but
not started still has real, numbered, authoritative Steps. That's exactly the state Phases 4
and 5 are in.

Two consequences worth stating, because each prevents a specific mistake:

- **A completed phase's list is not a Step list.** Phase 2's items summarize what survives — its
  item 1 was really Step 11, item 3 was Step 14, and Steps 1-9 were built then deleted.
  Renumbering it as Steps would contradict the archive and make a worse summary.
- **When a phase completes, its list gets rewritten** from Steps to What shipped. Nothing is
  lost: the numbering survives in the archived step documents and in the phase document's
  order-of-work chapter. A completed phase may drop the list entirely when the Status prose
  covers it — Phase 1 does.

---

## 📋 Detailed Milestone Breakdown

### Phase 1: Virtual Memory Management (VMM) & Demand Paging
* **Status**: **COMPLETE** ✅ — later revisited for an architecture-neutral redesign pass: single source of truth for the paging-level/index constants (was hand-copied in three functions), a real per-context demand-paging policy that inspects the actual fault reason (`arch_vmm_fault_is_present`/`arch_vmm_fault_is_write`) instead of ignoring it, a fixed memory leak in context teardown (previously only freed the root table, not the tree beneath it), and root-table setup/cloning delegated to per-architecture `arch_vmm_ensure_user_root()`/`arch_vmm_new_context_root()` so `vmm.c` no longer needs to explain x86_64-vs-AArch64 differences in its own comments. The working docs for both passes (`PHASE1_VMM.md`, `PHASE1_VMM_REDESIGN.md`) have been folded into this summary and removed now that the work is done.
* **Prerequisites**: Physical Memory Manager (PMM) — implemented in `kernel/mm/pmm.c`.
* **Goal**: Build page-table abstractions to isolate kernel memory space from user processes and support dynamic memory mapping.

---

### Phase 2: In-Memory Root Filesystem (Initramfs) & VFS
* **Status**: **COMPLETE** ✅ — a read/write in-memory root filesystem, populated at boot from the initrd, verified end-to-end by 31 VFS self-tests (Detailed working doc: [`archive/PHASE2_STEP14_RAMFS.md`](archive/PHASE2_STEP14_RAMFS.md), which supersedes [`archive/PHASE2_STEP13_TAR_DRIVER.md`](archive/PHASE2_STEP13_TAR_DRIVER.md)). Deliberately left for later: `truncate`, `unlink`, and `O_CREAT`/`O_TRUNC` in `sys_open` — none are needed until something actually deletes or replaces a file.
* **Goal**: Mount a real root filesystem with no disk driver at all, by reading a `ustar` archive Limine already loads into RAM as a boot module.
* **History**: The original plan targeted a disk-backed FAT filesystem over a hand-written VirtIO-block/PCI/ACPI stack ([`archive/PHASE2_VFS.md`](archive/PHASE2_VFS.md) Steps 1-9) — it worked, but wasn't clean enough to keep, and was fully removed (`git log`: *"rip out everything i found not clean enough, will redo later"*) in favor of the simpler initrd approach below. The VFS core (`fs/vfs/`) and its syscalls were untouched by the rip-out and remain exactly as originally built.
* **What shipped**:
  - ✅ **Iterative VFS path resolution** (`vfs_lookup`) — `sys_open`'s old single-segment shortcut replaced with real `/`-delimited path tokenization.
  - ✅ **Limine module parsing** — `module_request` registered in `boot.c`, `limine.conf` carries the module directive, `initrd.tar` built and staged by the Makefile.
  - ✅ **ramfs driver + TAR loader split** (`fs/ramfs/ramfs.c`, `fs/tar/tar.c`) — the archive format no longer *is* the filesystem. `ramfs` owns the storage (in-memory tree, first-child/next-sibling), and `tar_load()` is reduced to a parser that populates it purely through the public VFS path API — the same separation as Linux's `rootfs` + `unpack_to_rootfs()`. Originally `tar_mount()` did both jobs at once; making that writable would have made it a tmpfs wearing a tar costume, hence the split.
  - ✅ **Mount to VFS root `/`** — `ramfs_create_root()` mounted at `/` before anything else, then `tar_load()` fills it. Verified at boot: `/root` resolves through the mount table and `readdir` lists `hello.txt`.
  - ✅ **File content storage** — each node carries a sparse page list (`void **pages`, a `NULL` entry being a hole that reads as zeros), grown on demand via `krealloc` and backed page-by-page by the PMM. That is what real `tmpfs` does and what `mmap` will need in Phase 8. `lseek` past EOF then write produces a genuine hole, with no zero-fill pass and no pages allocated for the gap.

---

### Phase 3: Preemption — Timers, Tasks & the Scheduler
* **Status**: **IN PROGRESS** 🚧 — Step 1 of 3 is built. Working doc:
  [`PHASE3_PREEMPTION.md`](PHASE3_PREEMPTION.md).
* **Payoff**: two kernel threads interleaving their output. Preemptive multitasking, real and
  visible, **entirely in ring 0** — where a mistake prints a register dump instead of resetting
  the machine.
* **Starting line**: further along than it looks. The full trap frame exists on both
  architectures, a TSS is built and loaded, `vmm_create_context()`/`vmm_switch_context()`
  already give a process its own address space, and `VMM_USER` is plumbed to hardware.
* **Steps**:
  - ✅ **Step 1 — Timer & interrupts, no scheduler** — IDT gates above vector 31 + PIC remap +
    PIT + the LAPIC in virtual wire mode (x86_64); GICv2 + Generic Timer (AArch64). A monotonic
    tick counter to back `sys_clock_gettime`/`sys_nanosleep` later. Both architectures tick at
    100 Hz and shut down on their own. Broken into individually verifiable Parts in
    [`PHASE3_STEP1_TIMER.md`](archive/PHASE3_STEP1_TIMER.md).
  - ⬜ **Step 2 — `struct task`, per-task kernel stacks, trap-stub contract change** — the C
    handler returns the frame to restore instead of `void`, which is what makes a context
    switch four lines of assembly. Includes `tss.rsp0` (x86_64) maintenance, moving the kernel
    to EL1h so SP_EL0 can belong to userland (AArch64), and the `vectors.S` rewrite that
    follows from it. Still one task, so nothing observable changes. Broken into individually
    verifiable Parts in [`PHASE3_STEP2_KERNEL_STACKS.md`](PHASE3_STEP2_KERNEL_STACKS.md).
  - ⬜ **Step 3 — Two kernel threads, round-robin** — a second task with a fabricated frame
    whose `rip` points at a kernel function, both printing. **The milestone where preemption is
    real.**

---

### Phase 4: Privilege — Ring 3 / EL0 and the Syscall Boundary
* **Status**: **NOT STARTED** ⬜ — designed, not written. Working doc:
  [`PHASE4_PRIVILEGE.md`](PHASE4_PRIVILEGE.md).
* **Payoff**: a program the kernel does not trust calls `write` and the string appears on the
  console. The moment the OS becomes an OS.
* **Steps**:
  - ⬜ **Step 1 — Ring 3 / EL0 transition** — `iretq` (x86_64) / `eret` (AArch64) into a
    hand-fabricated user program, before any ELF loader exists. Needs user segments in the GDT,
    laid out to `sysret`'s constraint even though Step 2 is what enforces it. **Faulting on a
    privileged instruction is the success criterion** — it proves the drop happened.
  - ⬜ **Step 2 — Syscall entry path** — `svc` dispatch (AArch64, small: it lands in the
    existing vector table); `syscall`/`sysret` + `swapgs` + the `STAR`/`LSTAR`/`FMASK` MSRs
    (x86_64, not small). One syscall wired: `write`.

---

### Phase 5: Loading — The ELF Loader and a Real `/bin/init`
* **Status**: **NOT STARTED** ⬜ — designed, not written. Working doc:
  [`PHASE5_LOADING.md`](PHASE5_LOADING.md).
* **Payoff**: `/bin/init` — a real ELF you compiled, living in the initrd — loads, runs
  unprivileged, opens `/root/hello.txt` and prints it. The first moment PrOS runs *software*
  rather than running itself.
* **Steps**:
  - ⬜ **Step 1 — ELF64 loader & a freestanding `/bin/init`** — `PT_LOAD` segments mapped into
    the new context, `.bss` zeroed, initial user stack built (`argc`, `argv`, `envp`, `auxv`).
    An `init.c` built with the existing `zig cc` toolchain, no libc needed.
  - ⬜ **Step 2 — The real syscall surface** — per-process fd tables (they're kernel-global
    today), `copy_from_user` validation, `-errno` returns replacing `-1`, and
    `open`/`read`/`close`/`exit`/`getpid` reachable from ring 3.

---

### Phase 6: Talk Back — Serial Input, TTY & a Shell You Wrote
* **Status**: **NOT STARTED** ⬜ — undesigned; the scope below is a wish list, not a plan.
* **Payoff**: **you type at your OS and it answers.** Over `-serial stdio`, which is fully
  interactive. No libc, no keyboard driver, no VirtIO required.
* **Planned scope**:
  - **UART receive interrupt** on both architectures (PL011 on `virt`, 16550 on `q35`) feeding
    a byte queue. Deliberately *not* a PS/2 keyboard: QEMU `virt` has no i8042 at all, so PS/2
    isn't portable and VirtIO-input would mean rebuilding the VirtIO transport first.
  - **TTY line discipline** — canonical vs raw mode, echo, backspace, line buffering — behind
    `/dev/console`.
  - **`getdents64`**, replacing the internal `sys_readdir` at the ABI boundary.
  - **`psh`** — a freestanding shell, no libc, builtins only (`echo`, `ls`, `cat`, `help`).
    Builtins only because `fork`/`exec` don't exist yet, and an interactive prompt is worth
    having a phase early.

---

### Phase 7: Many Programs — `fork`, `exec`, Pipes & Signals
* **Status**: **NOT STARTED** ⬜ — undesigned; the scope below is a wish list, not a plan.
* **Payoff**: `psh` launches separate programs and pipes them together. `Ctrl+C` works.
* **Planned scope**:
  - **`switch_to` and blocking** — wait queues and voluntary switching, which the frame-swap
    scheduler of Phase 3 deliberately doesn't cover.
  - **`fork`** — eager copy first, copy-on-write once it works.
  - **`execve`, `wait4`, zombie reaping.**
  - **`pipe` + `dup2`**, and `|` in `psh`.
  - **Minimal signals** — `SIGINT` from `Ctrl+C`, `SIGCHLD`, `kill`; delivery frames on the
    user stack and `sigreturn`.
  - **FPU / SIMD context switching** lands here — the first time two *user* tasks coexist.
    AArch64 is nearly free (`-mgeneral-regs-only` means the kernel emits no FP at all); the
    x86_64 kernel currently *does* use SSE, so it needs `-mno-sse` to make lazy switching
    correct rather than approximately correct.

---

### Phase 8: Pixels — `/dev/fb0`
* **Status**: **NOT STARTED** ⬜ — undesigned; the scope below is a wish list, not a plan.
* **Payoff**: **a userland program you wrote draws on the screen.** A short phase with a large
  visual reward, deliberately placed to bank a win before the hard climb of Phase 9.
* **Planned scope**:
  - **`mmap`** — anonymous first, then file-backed over the sparse page list `ramfs` already
    uses.
  - **`/dev/fb0`** — expose the Limine framebuffer, with `FBIOGET_VSCREENINFO` /
    `FBIOGET_FSCREENINFO` `ioctl` handling.
  - **A freestanding drawing program**, proving the whole path without a libc.

---

### Phase 9: Real Unix — A C Library and BusyBox
* **Status**: **NOT STARTED** ⬜ — undesigned; the scope below is a wish list, not a plan.
* **Payoff**: `ls`, `cat`, `vi`, `sed`, `ash` — a genuine Unix userland.
* **Risk note**: this is the single riskiest phase in the project, because it's a *port*, not a
  build. It sits here deliberately: by now there is a working, interactive OS, so a bad month
  spent fighting a libc blocks nothing else.
* **Planned scope**:
  - **C library integration (`mlibc` / `Newlib`)** — map its syscall layer onto the kernel's
    Linux ABI dispatcher. Needs the full `auxv` from Phase 5, not just `AT_NULL`.
  - **A static hello world** that links and runs — the real acceptance test for the port.
  - **Cross-compile BusyBox** against it.
  - **`busybox sh` replaces `psh`**, and `/bin` gets populated.

---

### Phase 10: Terminals — PTYs and Local IPC
* **Status**: **NOT STARTED** ⬜ — undesigned; the scope below is a wish list, not a plan.
* **Payoff**: a shell running inside a pty, with job control.
* **Planned scope**:
  - **Pseudo-terminals (`/dev/ptmx` & `/dev/pts/*`)** — master/slave driver with termios
    `ioctl` support (`TCGETS`, `TCSETS`, `TIOCGWINSZ`, `TIOCSCTTY`). Mandatory before `xterm`.
  - **UNIX domain sockets (`AF_UNIX`)** — `socket`, `bind`, `connect`, `listen`, `accept`,
    `send`, `recv`, for `/tmp/.X11-unix/X0`.
  - **Job control** — process groups, sessions, controlling terminals, `SIGTSTP`/`SIGCONT`.

---

### Phase 11: Windows — `Xfbdev`, `lwm` & `xterm`
* **Status**: **NOT STARTED** ⬜ — undesigned; the scope below is a wish list, not a plan.
* **Payoff**: a graphical desktop, running on your own kernel.
* **Planned scope**:
  - **Real input devices** — PS/2 keyboard and mouse on `q35`, VirtIO-input on `virt` (which
    means a VirtIO transport, rebuilt from scratch), exposed as `/dev/input/event*`.
  - **Compile `Xfbdev`** — TinyX rendering directly to `/dev/fb0`.
  - **Compile `lwm` & `xterm`** — cross-compile the X11 client libraries (`libX11`, `libXext`,
    `libXt`, `libXaw`), then the window manager, then the terminal emulator.

---

### Phase 12: The Web — VirtIO Networking & `lynx`
* **Status**: **NOT STARTED** ⬜ — undesigned; the scope below is a wish list, not a plan.
* **Payoff**: browsing the web from inside PrOS.
* **Planned scope**:
  - **VirtIO-net driver** — PCI/MMIO packet transmit and receive.
  - **TCP/IP stack** — integrate lwIP or write one, exposing BSD socket syscalls (`sys_socket`,
    `sys_bind`, `sys_connect`, `sys_send`, `sys_recv`).
  - **Cross-compile `lynx` / `elinks`** with HTTP and DNS, to run inside `xterm` or on the
    framebuffer shell.
