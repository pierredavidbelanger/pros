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
ROADMAP.md                               Phase 4
  └─ PHASE4_PRIVILEGE.md                 Chapters 0-N   (read these)
     │                                   its Steps chapter lists the Steps (do these)
     └─ PHASE4_STEP1_*.md                Parts  C0 C1 · A1 · B1 B2 · C2 …
```

(Phase 3 is the worked example, now finished:
[`archive/PHASE3_PREEMPTION.md`](archive/PHASE3_PREEMPTION.md) with Chapters 0-8, and Chapter 6
listing three Steps that each got a document —
[`archive/PHASE3_STEP2_KERNEL_STACKS.md`](archive/PHASE3_STEP2_KERNEL_STACKS.md) ran Parts
C0 C1 · A1 · B1 B2 · C2 C3 C4.)

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
one fact** — does the phase have a working document — so there is no judgement call:

| State | Heading | What the list is | How much to trust it |
|---|---|---|---|
| has a working document | **Steps** | the ordered plan; item *n* **is** Step *n* | authoritative; mirrors that document, whether the phase is complete or not |
| no working document | **Planned scope** | a wish list for an undesigned phase | none — not a commitment |

A phase keeps its numbered **Steps** heading permanently once it has a working document —
completing the phase doesn't change the heading. An earlier version of this convention rewrote a
completed phase's list into a separate, non-numbered **What shipped** retrospective; Phase 4 and
Phase 5's closeouts are why that was dropped — the churn wasn't worth it, and a numbered list that
already mirrors the archived Step documents is just as authoritative after completion as before
it. A completed phase may still drop the list entirely when the Status prose covers it —
Phase 1 does.

Phase 2 and Phase 3 still carry the older **What shipped** heading, written before this
simplified — left as-is rather than rewritten to match. Phase 2's in particular is a genuine
retrospective, not just an old-style Steps list: its items summarize what survives, not what was
built in order — item 1 was really Step 11, item 3 was Step 14, and Steps 1-9 were built then
ripped out. Renumbering it as Steps would contradict the archive and make a worse summary, so it
stays a retrospective on its own merits, independent of which convention version wrote it.

---

## 📋 Detailed Milestone Breakdown

### Phase 1: Virtual Memory Management (VMM) & Demand Paging
* **Status**: **COMPLETE** ✅ — later revisited for an architecture-neutral redesign pass: single source of truth for the paging-level/index constants (was hand-copied in three functions), a real per-context demand-paging policy that inspects the actual fault reason (`arch_vmm_fault_is_present`/`arch_vmm_fault_is_write`) instead of ignoring it, a fixed memory leak in context teardown (previously only freed the root table, not the tree beneath it), and root-table setup/cloning delegated to per-architecture `arch_vmm_ensure_user_root()`/`arch_vmm_new_context_root()` so `vmm.c` no longer needs to explain x86_64-vs-AArch64 differences in its own comments. The working docs for both passes (`PHASE1_VMM.md`, `PHASE1_VMM_REDESIGN.md`) have been folded into this summary and removed now that the work is done.
* **Prerequisites**: Physical Memory Manager (PMM) — implemented in `kernel/src/mm/pmm.c`.
* **Goal**: Build page-table abstractions to isolate kernel memory space from user processes and support dynamic memory mapping.

---

### Phase 2: In-Memory Root Filesystem (Initramfs) & VFS
* **Status**: **COMPLETE** ✅ — a read/write in-memory root filesystem, populated at boot from the initrd, verified end-to-end by 31 VFS self-tests (Detailed working doc: [`archive/PHASE2_STEP14_RAMFS.md`](archive/PHASE2_STEP14_RAMFS.md), which supersedes [`archive/PHASE2_STEP13_TAR_DRIVER.md`](archive/PHASE2_STEP13_TAR_DRIVER.md)). Deliberately left for later: `truncate`, `unlink`, and `O_CREAT`/`O_TRUNC` in `sys_open` — none are needed until something actually deletes or replaces a file.
* **Goal**: Mount a real root filesystem with no disk driver at all, by reading a `ustar` archive Limine already loads into RAM as a boot module.
* **History**: The original plan targeted a disk-backed FAT filesystem over a hand-written VirtIO-block/PCI/ACPI stack ([`archive/PHASE2_VFS.md`](archive/PHASE2_VFS.md) Steps 1-9) — it worked, but wasn't clean enough to keep, and was fully removed (`git log`: *"rip out everything i found not clean enough, will redo later"*) in favor of the simpler initrd approach below. The VFS core (`fs/vfs/`) and its syscalls were untouched by the rip-out and remain exactly as originally built.
* **What shipped**:
  - ✅ **Iterative VFS path resolution** (`vfs_lookup`) — `sys_open`'s old single-segment shortcut replaced with real `/`-delimited path tokenization.
  - ✅ **Limine module parsing** — `module_request` registered in `boot.c`, `limine.conf` carries the module directive, the initrd archive built per-architecture and staged by `initrd/Makefile`.
  - ✅ **ramfs driver + TAR loader split** (`fs/ramfs/ramfs.c`, `fs/tar/tar.c`) — the archive format no longer *is* the filesystem. `ramfs` owns the storage (in-memory tree, first-child/next-sibling), and `tar_load()` is reduced to a parser that populates it purely through the public VFS path API — the same separation as Linux's `rootfs` + `unpack_to_rootfs()`. Originally `tar_mount()` did both jobs at once; making that writable would have made it a tmpfs wearing a tar costume, hence the split.
  - ✅ **Mount to VFS root `/`** — `ramfs_create_root()` mounted at `/` before anything else, then `tar_load()` fills it. Verified at boot: `/root` resolves through the mount table and `readdir` lists `hello.txt`.
  - ✅ **File content storage** — each node carries a sparse page list (`void **pages`, a `NULL` entry being a hole that reads as zeros), grown on demand via `krealloc` and backed page-by-page by the PMM. That is what real `tmpfs` does and what `mmap` will need in Phase 8. `lseek` past EOF then write produces a genuine hole, with no zero-fill pass and no pages allocated for the gap.

---

### Phase 3: Preemption — Timers, Tasks & the Scheduler
* **Status**: **COMPLETE** ✅ — all three Steps built, across three step documents. **PrOS
  preempts**: three tasks (the boot task plus two kernel threads) take turns on the CPU driven
  only by the timer interrupt, their output interleaves, and the switch counters come out
  near-equal — `34 / 67 / 100` per task at the one-, two- and three-second marks on AArch64,
  against a predicted `300 / 3`. Nobody yields; nothing is cooperative. Working doc:
  [`archive/PHASE3_PREEMPTION.md`](archive/PHASE3_PREEMPTION.md), with
  [`Step 1`](archive/PHASE3_STEP1_TIMER.md),
  [`Step 2`](archive/PHASE3_STEP2_KERNEL_STACKS.md) and
  [`Step 3`](archive/PHASE3_STEP3_ROUND_ROBIN.md) beneath it. One named thing was deliberately
  left open and is recorded in Step 3's retrospective: `SCHED_TIME_SLICE_TICKS` was never defined
  (the slice is one tick, but the knob has no name). `-mno-sse` also remains open and belongs to
  Phase 7.
* **Payoff**: two kernel threads interleaving their output. Preemptive multitasking, real and
  visible, **entirely in ring 0** — where a mistake prints a register dump instead of resetting
  the machine.
* **What shipped**:
  - ✅ **Timers and interrupt controllers on both architectures** — IDT gates above vector 31 +
    PIC remap + PIT + the LAPIC in virtual wire mode (x86_64); GICv2 + Generic Timer (AArch64),
    feeding one architecture-neutral monotonic tick counter at 100 Hz that will back
    `sys_clock_gettime`/`sys_nanosleep` later. The LAPIC part wasn't in the plan and turned out
    not to be optional.
  - ✅ **The trap-stub contract change** — the C handler returns *the frame to restore* instead
    of `void`, which is what reduces a context switch to four lines of assembly. This is the
    single idea the whole phase rests on: the trap frame *is* the process.
  - ✅ **Per-task kernel stacks** with guard values, `tss.rsp0` maintenance (x86_64), and the
    move to EL1h so `SP_EL0` can belong to userland (AArch64) — plus the `vectors.S` rewrite
    that follows from it, and the correctness fixes it swept up (`tpidrro_el0` no longer
    clobbered, the interrupted `SP` saved from the right place).
  - ✅ **`struct task`, a circular run queue, and round-robin scheduling** — `task_create()`
    allocating a stack and fabricating an initial trap frame via the one new `arch_*` hook,
    `sched_add_task()`, and a `sched_pick_next()` that advances. Preemption is decided in the
    timer path (`need_resched`) and acted on at exactly one point — trap exit — so the kernel is
    *preempted at trap exit* rather than preemptible, which is a much easier thing to get right.
  - ✅ **Diagnosability, which paid for itself repeatedly** — `switch_count` per task (three
    near-equal counters prove round-robin where interleaved output alone does not),
    `task_owns_frame()` as a live invariant on every trap, stack high-water marks, and
    `task_dump_all()` from both architectures' panic paths so every crash answers "which task
    was running?".
  - ✅ **11 new self-tests** — five `[KSTK ]` and six `[TASK ]`, including one that pins shut a
    latent panic Step 2 left behind (asking `kstack_guard_intact()` about a task that has no
    guard, i.e. the boot task on Limine's stack).

---

### Phase 4: Privilege — Ring 3 / EL0 and the Syscall Boundary
* **Status**: **COMPLETE** ✅ — both Steps built, on both architectures. **A program the kernel
  does not trust calls `write(1, "hello from ring 3\n", 18)` from ring 3 / EL0, and the string
  appears on the console** — then the same task deliberately executes a privileged instruction and
  faults, proving the machine survived the syscall cleanly and privilege is still enforced.
  Working doc: [`archive/PHASE4_PRIVILEGE.md`](archive/PHASE4_PRIVILEGE.md).
* **Payoff**: a program the kernel does not trust calls `write` and the string appears on the
  console. The moment the OS becomes an OS.
* **Steps**:
  - ✅ **Step 1 — Ring 3 / EL0 transition** — `iretq` (x86_64) / `eret` (AArch64) into a
    hand-fabricated user program, before any ELF loader exists. User segments added to the GDT,
    laid out to `sysret`'s constraint even though Step 2 is what enforces it. **Faulting on a
    privileged instruction is the success criterion** — verified on both architectures (`#GP` on
    x86_64, a trapped `mrs` on AArch64), plus three follow-up experiments confirming the
    enforcement is real (kernel-address read, W^X write, bare `ret`). Individually verifiable
    Parts in [`archive/PHASE4_STEP1_RING3_EL0.md`](archive/PHASE4_STEP1_RING3_EL0.md), which also
    documents an AArch64 `EC` correction found along the way and a real VMM demand-paging bug
    (`try_handle_hhdm_mmio_fault()` missing a presence check) found and fixed in the process.
  - ✅ **Step 2 — Syscall entry path** — `svc` dispatch (AArch64, small: it lands in the
    existing vector table); `syscall`/`sysret` + `swapgs` + the `STAR`/`LSTAR`/`FMASK` MSRs
    (x86_64, not small — a hand-built `struct trap_frame` from a standing start, no hardware
    frame, no free register). One syscall wired: `write`, dispatched through a table indexed by
    Linux syscall numbers. Verified end to end on both architectures, plus bounds/adversarial
    checks (bad syscall number, bad fd) on both. A real bug found and fixed along the way:
    AArch64's IRQ handling only checked vector slot 5 (IRQs interrupting kernel code), missing
    slot 9 (IRQs interrupting EL0 code) — the first time in the project an interrupt ever had to
    land mid-userland-execution, which produced a silent live-lock, not a crash. Individually
    verifiable Parts in [`archive/PHASE4_STEP2_SYSCALL.md`](archive/PHASE4_STEP2_SYSCALL.md).

---

### Phase 5: Loading — The ELF Loader and a Real `/bin/init`
* **Status**: **COMPLETE** ✅ — both Steps done and verified on both architectures. Working doc:
  [`PHASE5_LOADING.md`](archive/PHASE5_LOADING.md).
* **Payoff**: `/bin/init` — a real ELF you compiled, living in the initrd — loads, runs
  unprivileged, opens `/root/hello.txt` and prints it. The first moment PrOS runs *software*
  rather than running itself.
* **Steps**:
  - ✅ **Step 1 — ELF64 loader & a freestanding `/bin/init`** — `PT_LOAD` segments mapped into
    a private context, `.bss` zeroed, a real initial user stack built (`argc`, `argv`, `envp`,
    `auxv`). `init.c` built with the existing `zig cc` toolchain, no libc needed. `/bin/init`
    loads, calls the real `write` syscall, and runs unprivileged alongside `BOOT` under
    preemptive scheduling — on both x86_64 and AArch64. Individually verifiable Parts in
    [`PHASE5_STEP1_ELF_LOADER.md`](archive/PHASE5_STEP1_ELF_LOADER.md).
  - ✅ **Step 2 — The real syscall surface** — per-process fd tables (were kernel-global before),
    `copy_from_user`/`copy_to_user` validation, `-errno` returns replacing `-1`, and
    `open`/`openat`/`read`/`close`/`exit`/`getpid` reachable from ring 3. `/bin/init` opens
    `/root/hello.txt`, reads it, writes the real contents to stdout, and exits — staying
    genuinely dead afterward. Surfaced a real x86_64-only bug: `syscall_entry.S` always used
    `sysretq` (return-to-ring-3-only) on the way out, which broke the moment `exit` caused a
    switch to the kernel-thread `BOOT` instead of back to `init`'s own userspace — fixed by
    switching to `iretq` (general-purpose, reads the target ring off the frame), same instruction
    the timer-interrupt path already used. Individually verifiable Parts in
    [`PHASE5_STEP2_SYSCALL_SURFACE.md`](archive/PHASE5_STEP2_SYSCALL_SURFACE.md).

---

### Phase 6: Talk Back — Serial Input, TTY & a Shell You Wrote
* **Status**: **COMPLETE** ✅ — all three Steps done and verified on both architectures. **A
  keystroke now makes it all the way in**: interrupt → shared byte queue → line discipline →
  `/dev/console` → a userland `read()` → a shell that answers. Two things landed that no Step
  designed, both fallout from Step 2's decision 4 being wrong about interrupt state: **every
  syscall is now preemptible**, and PrOS has its **first synchronization primitive**
  (`struct spinlock`, irqsave-only) — both Phase 7 groundwork arriving early. Working doc:
  [`PHASE6_TALK_BACK.md`](archive/PHASE6_TALK_BACK.md).
* **Payoff**: **you type at your OS and it answers.** `psh$ ls /root` → `hello.txt`;
  `cat /root/hello.txt` → the file; `exit` → the machine shuts down. Over `-serial stdio`, fully
  interactive, no libc, no keyboard driver, no VirtIO required. Only the phase as a whole needed
  to end there — an individual Step doesn't need its own standalone demo, as long as every Step
  is actually used by the final payoff.
* **Steps**:
  - ✅ **Step 1 — UART receive interrupt + byte queue** on both architectures (PL011 SPI 33 on
    `virt`, 16550 IRQ 4 on `q35`), feeding one shared, architecture-neutral 256-byte ring buffer.
    Deliberately *not* a PS/2 keyboard: QEMU `virt` has no i8042 at all, so PS/2 isn't portable and
    VirtIO-input would mean rebuilding the VirtIO transport first. `gic_enable_irq()` generalized
    to any `GICD_ISENABLERn` rather than duplicated for the SPI, and the ISR drains in a loop —
    one interrupt can stand for several queued bytes. Individually designed in
    [`PHASE6_STEP1_UART_INPUT.md`](archive/PHASE6_STEP1_UART_INPUT.md), whose retrospective
    records the Step's own latent defect: `console_input_init()` was written and never called, which made the
    queue silently drop everything and cost most of Step 2's debugging.
  - ✅ **Step 2 — TTY line discipline + `/dev/console`** — canonical mode, echo, destructive
    backspace, line buffering, exposed as a real `VFS_CHARDEVICE` node mounted at `/dev/console`
    and pre-opened onto every task's fds `0`/`1`/`2`. Phase 5 Step 2's deferred decision 2(b)
    lands here too: `sys_write_console_or_vfs`'s fd `1`/`2` shim is gone, so `write(1, …)` is now
    an ordinary `copy_from_user`-validated path to a real file descriptor. Its design was wrong
    about one load-bearing fact — that interrupts stay enabled during a syscall — which wedged the
    machine on the first `read()` and is corrected in place in
    [`PHASE6_STEP2_TTY_CONSOLE.md`](archive/PHASE6_STEP2_TTY_CONSOLE.md), along with a
    retrospective on why identical failure across two independent drivers was read backwards.
  - ✅ **Step 3 — `getdents64` and `psh`** — the internal `sys_readdir` becomes the real,
    Linux-ABI-shaped syscall (packed variable-length `struct linux_dirent64`, one `copy_to_user`
    at the boundary, which also closed a latent hole where a driver `snprintf`'d straight into an
    unvalidated ring-3 pointer), in service of `/bin/psh` — a freestanding shell with builtins
    only (`help`, `echo`, `cat`, `ls`, `exit`). Builtins only because `fork`/`exec` don't exist
    yet, and an interactive prompt is worth having this early. Two pieces of machinery came with
    it: a `pros.init=` kernel command line knob, so `/bin/init` stays bootable and the Phase 5
    demo stays runnable; and `sched_only_current_is_alive()`, which replaces the boot task's
    10-second timer — **the machine now shuts down because the last program ended**, which is
    what makes a session last as long as the human wants. Designed against the built tree rather
    than planned ahead of it, so it inherited a real console, a preemptible syscall path and a
    spinlock that none of the original planning assumed. `ls` is honestly limited — mounts are
    invisible to `readdir`, so `ls /dev` finds nothing, and `ls <file>` needs a `stat` that
    doesn't exist yet. Working doc:
    [`PHASE6_STEP3_GETDENTS_PSH.md`](archive/PHASE6_STEP3_GETDENTS_PSH.md).

---

### Phase 7: Many Programs — `fork`, `exec`, Pipes & Signals
* **Status**: **IN PROGRESS** 🚧 — **Step 1 is done on both architectures**; Steps 2-5 are still
  chapters written against the tree as Phase 6 left it. Ten
  decisions named. **The three that gated Step 1 are resolved and now built:** a real `switch_to` —
  which the per-task kernel stacks of Phase 3 Step 2 make a dozen instructions rather than an
  architecture — with `sched_on_trap_exit()` becoming a caller of it rather than a peer, xv6's
  dedicated-scheduler-thread shape, and `BOOT` filling that role from outside the run queue; plus
  xv6-style channels for `sleep`/`wakeup`, with `poll` named in advance as what retires them; plus
  the `/dev/console` line buffer staying per-node, which is where a line discipline belongs, with
  a `struct tty` and a named reader replacing what was mis-stated as a per-open gap. The other
  seven wait for built code to decide against. Working doc:
  [`PHASE7_MANY_PROGRAMS.md`](PHASE7_MANY_PROGRAMS.md).
* **Payoff**: `ls /root | cat` runs **two separate programs** connected by a pipe. `Ctrl+C` kills
  a program that isn't listening. A process that ends is reaped by its parent, rather than left as
  a corpse in the run queue forever.
* **Steps** (proposed by the working doc; each gets its own document when it starts):
  - ✅ **Step 1 — Blocking and wait queues** — the piece Phase 3's frame-swap scheduler
    deliberately doesn't cover, now built: `switch_to` on both architectures, `BOOT` promoted to a
    dedicated scheduler thread outside the run queue, and xv6-style `sleep`/`wakeup` on a channel.
    Two prerequisites had already landed early in Phase 6 Step 2: syscalls
    are preemptible, and `struct spinlock` exists. Retires the first of the two known
    `/dev/console` stopgaps — `read()` spinning instead of sleeping. The second was mis-stated:
    the line buffer belongs per-node, and what is actually missing is arbitration between two
    readers, which waits for Step 2, since nothing can contend for the console until `fork`
    exists. **Verified** by `Ctrl+P` ten seconds apart: the shell frozen at the same
    `switch_count`, `BLOCKED`, while the machine idles in `wfi`/`hlt`. Working doc:
    [`PHASE7_STEP1_BLOCKING.md`](PHASE7_STEP1_BLOCKING.md).
  - ⬜ **Step 2 — `fork`, `wait4`, reaping, and FPU/SIMD state** — the first two user processes,
    and the first one that gets cleaned up. Eager copy first, copy-on-write once it works. Carries
    a collision worth seeing early: reaping is the direct enemy of the shutdown predicate Phase 6
    Step 3 built, which terminates *because* dead tasks are never removed from the ring. FPU state
    lands here because this is the first moment two *user* tasks coexist — AArch64 is nearly free
    (`-mgeneral-regs-only`), x86_64 needs `-mno-sse` first. Working doc:
    [`PHASE7_STEP2_FORK.md`](PHASE7_STEP2_FORK.md).
  - ⬜ **Step 3 — `execve`, plus `stat`/`fstat`** — `psh` runs a real child process and gets its
    prompt back. `stat` is already owed: without it Phase 6's `ls <file>` can't tell a regular
    file from a directory it failed to read, and BusyBox wants it long before anything else does.
  - ⬜ **Step 4 — `pipe`, `dup2`, and `|` in `psh`** — the Step that makes the payoff sentence
    true.
  - ⬜ **Step 5 — Minimal signals** — `SIGINT` from `Ctrl+C`, `SIGCHLD`, `kill`; delivery frames
    on the user stack and `sigreturn`. Splittable into its own phase if it grows past its worth.

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
