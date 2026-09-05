# Working Document: Phase 7 Step 2 — `fork`, `wait4`, Reaping, and FPU State [STATUS: NOT STARTED ⬜]

> [!NOTE]
> **Phase 7 Step 2**, from [`PHASE7_MANY_PROGRAMS.md`](PHASE7_MANY_PROGRAMS.md) Chapters 2 and 3,
> minus `execve`. That document's **Decisions 4, 5 and 6 are still open** and belong here, as does
> the second half of Decision 3 (console arbitration). This document is what those questions look
> like against the tree as [Step 1](PHASE7_STEP1_BLOCKING.md) left it on 2026-08-31.

> [!NOTE]
> Mentor-mode reminder (see the note at the top of [`../README.md`](../README.md)): this is a
> design reference to code from by hand, not code to paste in. The seven decisions below are
> Step-level and **open** — and, unlike Step 1's first draft, **they carry no recommendation.**
> Each lays out what the tree says and what the field does, then ends with the trace that settles
> it. The argument is the work, and it is deliberately not written here.

Tracks: **`C`** architecture-neutral · **`A`** x86_64 · **`B`** AArch64.

---

## 🎯 What "done" looks like

> A user program calls `fork()`. **Two** processes come back from that one call, each with its own
> address space, and both print. The parent calls `wait4()` and **sleeps** — not spins — until the
> child exits, then prints the child's exit code. The child's `struct task`, kernel stack and page
> tables are **freed**, the page count returns to what it was before the `fork`, and `exit` at the
> prompt still shuts the machine down.

This is the first time the word *process* means something in PrOS: until now there has been
exactly one user task, built by hand in `main.c`, and the only way a program ended was to leave a
corpse in the run queue for the shutdown check to count. After this Step, programs create programs
and clean up after them.

**What is explicitly *not* in this Step:**

- ❌ **No `execve`.** A child is a copy of its parent and stays one. Step 3 makes it a different
  program, which is why the demo is a program that forks *itself*.
- ❌ **No copy-on-write.** Eager copy is the Step. Whether eager copy is written in a shape CoW can
  grow out of is Decision 3; writing CoW is not.
- ❌ **No `stat`, no pipes, no signals.** Steps 3, 4, 5. `SIGCHLD` in particular is named here
  only so that the exit path is not written in a way that makes it hard later.
- ❌ **No `PATH`, no `&`, no job control.** `wait4` waits for one child or any child, synchronously;
  nothing here runs in the background.
- ❌ **No `-mno-sse` — literally.** Decision 5 has a surprise in it: that flag has never done
  anything in this tree, and the honest version of "add `-mno-sse` first" is a different flag.

---

## 🧠 Where this actually stands

Confirmed by reading the tree, not assumed:

- **`struct task` has no parent, no children, no exit status, no zombie state.**
  [`task.h:14-27`](../kernel/include/proc/task.h:14) is `pid`, `state`, `name`, the two stacks
  pointers, the two frames, `vmm_context`, `switch_count`, `fds[256]`, `chan`, `next`. States are
  `DEAD`/`READY`/`RUNNING`/`BLOCKED`, and `task_state_names[]`
  ([`task.c:16`](../kernel/src/proc/task.c:16)) is still a plain array indexed by state.
- **`sys_exit()` throws the status away.** [`task.c:166-170`](../kernel/src/proc/task.c:166) —
  `(void)status; sched_exit_current();` — and `sched_exit_current()`
  ([`sched.c:84-89`](../kernel/src/proc/sched.c:84)) marks the task `TASK_DEAD` and sets
  `need_resched`. **Nothing frees anything.** The task, its 16 KiB kernel stack, its page tables
  and its fds all outlive it, forever, in the ring.
- **The shutdown predicate is no longer in conflict with reaping — but it is with zombies.** Step 1
  absorbed `sched_only_current_is_alive()` into `sched_any_alive()`
  ([`sched.c:31-39`](../kernel/src/proc/sched.c:31)): "does the ring hold any task that is not
  `DEAD`". Removing a `DEAD` task from the ring cannot change that answer, so the collision Phase 7
  Chapter 3 predicted is mostly gone. What is left is exact: **any state that is not `DEAD` counts
  as alive**, so a task that lingers waiting to be reaped keeps the machine up. Decision 4 is where
  that bites.
- **The run queue is a singly-linked ring with two anchors.** `run_queue_head`
  ([`sched.c:11`](../kernel/src/proc/sched.c:11)) and `cursor`
  ([`sched.c:12`](../kernel/src/proc/sched.c:12), where round-robin resumes looking).
  `sched_add_task()` ([`sched.c:46-54`](../kernel/src/proc/sched.c:46)) inserts **after the head**,
  never at a tail. Four walks go round it — `sched_pick_next()`, `sched_any_alive()`, `wakeup()`,
  `sched_task_dump_all()` — and **nothing has ever removed a node from it.** There is no
  predecessor pointer.
- **`sleep()` demands a lock, and `sched.c` has none.** [`sched.c:101-114`](../kernel/src/proc/sched.c:101)
  panics on `sleep(chan, NULL)`. The console hands it `console_dev_node_data.lock`; a parent
  sleeping in `wait4` has to hand it *something*, and today there is no lock guarding task state,
  the ring, or anything a child's `exit` would touch on its parent's behalf.
- **`current->trap_frame` is written at trap *exit*, not entry** —
  [`sched.c:69`](../kernel/src/proc/sched.c:69), inside `sched_on_trap_exit()`, which runs *after*
  `syscall_dispatch()` returns. So during a syscall the field holds the frame of the *previous*
  trap. The comment on the field ([`task.h:20`](../kernel/include/proc/task.h:20), "rewritten on
  every kernel entry") describes what Step 1's Decision 1 intended, not what the code does. It has
  never mattered, because nothing has ever read a trap frame from inside a syscall. `fork` is the
  first thing that has to. Decision 1.
- **The user-entry frame always lands at the same place on the kernel stack.** On x86_64 both
  entry paths start from the stack top — `syscall_entry.S` from `%gs:0`
  ([`syscall_entry.S:154`](../kernel/src/arch/x86_64/syscall_entry.S:154)), the IDT path from
  `tss.rsp0` — and push the same 22 slots, so a frame from ring 3 sits at `kernel_stack_top - 176`,
  which is where `arch_task_init_user_frame()`
  ([`x86_64/arch.c:250-260`](../kernel/src/arch/x86_64/arch.c:250)) fabricates one too. On AArch64
  it is `SP_EL1 - TRAP_FRAME_SIZE` ([`vectors.S:11`](../kernel/src/arch/aarch64/vectors.S:11)),
  matching [`aarch64/arch.c:249-257`](../kernel/src/arch/aarch64/arch.c:249). A frame from a trap
  taken *in the kernel* — a timer tick during a syscall — lands deeper, wherever `sp` was.
- **The syscall return value is written into the frame *after* the handler returns.**
  [`idt.c:216`](../kernel/src/arch/x86_64/idt.c:216) `frame->rax = ret` and
  [`exceptions.c:103`](../kernel/src/arch/aarch64/exceptions.c:103) `frame->x[0] = ret`. A frame
  copied *during* `sys_fork` still carries the syscall number in `rax` / the first argument in
  `x0`. The parent's frame gets the pid for free at exit; the child's copy does not.
- **`task_inner_create()` always opens fds 0/1/2 on the console**
  ([`task.c:63`](../kernel/src/proc/task.c:63) via `task_open_std_fds()`). Every task born so far
  wanted that. A forked child does not: it must hold `file_ref()`s to its parent's
  `struct file`s — the same open file, the same offset — and `file_ref()`
  ([`file.c:28-38`](../kernel/src/fs/vfs/file.c:28)) is already the whole fd-table half of `fork`.
- **`task_destroy()` frees the kernel stack it is standing on if called from the task itself**
  ([`task.c:104-108`](../kernel/src/proc/task.c:104)). It was written for the self-test, which
  destroys a task that never ran. It has never been called on a task that has.
- **The scheduler thread runs on whatever address space the last task left loaded.** `scheduler()`
  calls `vmm_switch_context(next->vmm_context)` on the way *out*
  ([`sched.c:144`](../kernel/src/proc/sched.c:144)) and never switches back on the way in. While
  it idles, CR3 / `TTBR0_EL1` still name the previous task's root table — and on x86_64 that root
  page also holds the cloned upper-half entries the kernel is executing through.
- **The VMM can build and tear down a lower half but cannot read one.** `vmm_destroy_context()`
  ([`vmm.c:90-106`](../kernel/src/mm/vmm.c:90)) walks root entries 0..255 recursively and frees
  every frame it finds. There is no walk that *visits* pages without freeing them, and **no
  primitive that turns a hardware PTE back into abstract `VMM_*` flags** —
  [`arch.h`](../kernel/include/arch/arch.h:21) has `arch_vmm_make_pte()`, `_pte_is_present()`,
  `_pte_get_phys()`, and nothing in the other direction. `elf_load()`'s second pass
  ([`elf.c:151-172`](../kernel/src/proc/elf.c:151)) is why that matters: text is mapped read-only
  and non-writable data is `NO_EXECUTE`, and Phase 4 proved those bits are enforced. A copy that
  cannot read them cannot preserve them.
- **`struct vmm_context` carries a demand-paging range** (`demand_page_lo/hi`,
  [`vmm.h:32-33`](../kernel/include/mm/vmm.h:32)) that `main.c` never sets for `init` — so a user
  stack is exactly the one page `elf_build_user_stack()` maps, and nothing more appears on fault.
  A child needs the same range copied, whatever it is.
- **`copy_to_user()` exists** ([`uaccess.c:17-24`](../kernel/src/mm/uaccess.c:17)) and is what
  `wait4`'s `wstatus` pointer goes through. **`ECHILD` does not** —
  [`errno.h`](../kernel/include/errno.h) has eleven values and none of `ECHILD`, `EINTR`,
  `ESRCH`.
- **The syscall numbers are not symmetric.** [`unistd.h`](../kernel/include/asm/unistd.h) is where
  they go, and Linux's tables say: x86_64 has `fork` 57, `vfork` 58, `clone` 56, `wait4` 61,
  `exit_group` 231. **AArch64 has no `fork` at all** — only `clone` 220, `wait4` 260,
  `exit_group` 94 — and every libc on that architecture forks by calling `clone(SIGCHLD, 0, …)`.
  The tree already has one PrOS-only number for a syscall AArch64 lacks (`SYS_open` 511,
  [`unistd.h:5`](../kernel/include/asm/unistd.h:5)). Decision 2.
- **`pid` 0 is `BOOT` and `pid` 1 is `init`**, by accident of `next_pid` starting at 0
  ([`task.c:13`](../kernel/src/proc/task.c:13)). Unix's swapper is pid 0 and its `init` is 1. The
  accident is worth keeping.
- **The x86_64 `swapgs` fuse Step 1 named is already defused.** Both the entry
  ([`isr.S:71-74`](../kernel/src/arch/x86_64/isr.S:71)) and the one exit tail
  ([`isr.S:119-121`](../kernel/src/arch/x86_64/isr.S:119)) decide from the frame's `cs`, not from
  the path. Two user tasks can now switch through either stub. Listed so nobody re-derives it.
- **Step 1 left two things at this Step's door.** `need_resched` is a single global that goes stale
  across an idle period, so the first trap exit after a wakeup spends a resched nobody asked for —
  harmless with one task, a fairness question with two. And type-ahead is gone: a byte arriving
  while a finished line sits undrained is dropped, which a long-running child makes easy to hit.
  Neither blocks this Step; both are named in Step 1's closeout with the fix that goes with them.

### On the FPU — measured, not remembered

Because Decision 5 turns entirely on facts, they were measured rather than read off the configs:

- **SSE is not enabled on x86_64.** Under QEMU with gdb stopped at `scheduler()`:
  `cr0 = 0x80010011` (`PE`, `ET`, `WP`, `PG`), `cr4 = 0x20` (`PAE` only). `CR4.OSFXSR` (bit 9)
  and `OSXMMEXCPT` (bit 10) are clear, so **any SSE instruction, kernel or user, raises `#UD`.**
  That is what the Limine protocol promises for base revision 5+ — "all other `cr0`, `cr4` and
  `EFER` bits are cleared" — and [`boot.c:5`](../kernel/src/core/boot.c:5) asks for revision 6.
- **Nothing has ever executed one.** The x86_64 kernel contains 205 `xmm` instructions, all in the
  `printf` family: 40 of them are the varargs prologues of `kprintf`/`printf_`/`sprintf_`/
  `snprintf_`/`fctprintf`, skipped by `test %al, %al` because no caller passes a float, and the
  rest are `_ftoa`/`_etoa`, reachable only through a `%f` nobody has written. `psh` and `init`
  contain zero `xmm` instructions on x86_64 and zero `q`/`v` register uses on AArch64.
- **AArch64 has the FPU on, for everyone, and saves nothing.** `arch_init()` sets
  `CPACR_EL1.FPEN = 0b11` ([`aarch64/arch.c:14-20`](../kernel/src/arch/aarch64/arch.c:14)): no
  trapping at EL0 or EL1. `struct trap_frame` is `x0`-`x30` plus system registers; `switch_frame`
  is `x19`-`x30`. No `q`, no `fpcr`, no `fpsr`, anywhere.
- **`-mgeneral-regs-only` and `-mno-sse` are silently dropped by `zig cc` 0.14.1.** A
  `double f(double x) { return x * 2; }` compiles without complaint under both flags; under either,
  `kprintf`'s prologue still spills `xmm0`-`xmm7` / `q0`-`q7`. So the sentence in Step 1's design
  — *"AArch64 saves no FP registers anywhere, because `-mgeneral-regs-only` means the kernel emits
  none"* — was wrong in fact and right by luck: the AArch64 kernel executes `str q0, [sp, …]` on
  every `kprintf`, allowed by `FPEN` and harmless because it only stores caller-saved registers.
  `zig cc`'s own syntax *is* honoured: `-mcpu=x86_64-sse-sse2-mmx+soft_float` and
  `-mcpu=generic-fp_armv8-neon` both make that `double` function a compile error.
- **With the honoured flags, `printf.c` is what breaks** — `_ftoa` "requires `double` type
  support" — and `-DPRINTF_DISABLE_SUPPORT_FLOAT` fixes it. Built that way, **both kernels link with
  zero FP/SIMD instructions** (measured: 0 `xmm`/x87/`mm` on x86_64, 0 `q`/`v`/`d` on AArch64).
  Without `+soft_float` on x86_64 the failure is inside zig's bundled compiler-rt
  (`__extendhfsf2`: "SSE register return with SSE disabled"), not in this tree —
  [`libs/cc-runtime`](../kernel/Makefile:41) is cloned by the Makefile and never compiled, so
  zig's runtime is the one in use.

---

## 🧬 The child is a brand-new task wearing its parent's frame

The single most useful thing to have straight before writing any of this: **`fork` does not copy
the parent's kernel stack. It copies one struct off it.**

The Phase 7 document says `fork` "returns twice", which is true from userland and misleading from
the kernel. The parent returns from `sys_fork` the ordinary way — up through `syscall_dispatch`,
out through its trap frame, `rax`/`x0` holding the child's pid. The child **never executes a single
instruction of `sys_fork`**. It is born the way every task since Phase 3 has been born:

```
parent's kernel stack                    child's kernel stack
high ┌──────────────────────┐            ┌──────────────────────┐ <- kstack_get_top
     │ trap_frame           │ ── copy ─> │ trap_frame           │   rax/x0 := 0, nothing else changes
     ├──────────────────────┤            ├──────────────────────┤
     │ syscall_dispatch     │            │ switch_frame         │   x30/ret = task_trampoline
     │ sys_fork             │            │                      │
     │ …                    │            │                      │
low  └──────────────────────┘            └──────────────────────┘
```

The child's kernel stack holds a trap frame and a fabricated switch frame and nothing between them
— the same two structs `task_create_user()` lays down
([`task.c:84-90`](../kernel/src/proc/task.c:84)), the same
`arch_task_init_switch_frame()` pointing the first switch at `task_trampoline`, the same tail out.
The only difference from a task `main.c` builds is *where the trap frame's contents came from*:
`task_create_user()` invents them, `fork` copies ones the hardware really wrote. Step 1's invariant
holds without exception:

```
scheduler picks the child ──> switch_frame ──> task_trampoline ──> trap_frame ──> userland, rax/x0 = 0
```

Two consequences to keep in view:

- **The live C call chain is the parent's alone.** Nothing in the middle of the parent's stack —
  locals, the half-finished `sys_fork`, the spinlock flags — exists in the child. That is *why* the
  copy is one `memcpy` of 176 or 304 bytes and not a stack clone, and why the child needs the
  trampoline that Step 1's Decision 2 built for reasons that looked like SMP paranoia at the time.
- **The frame has to sit where a real one would.** Not because `task_owns_frame()` cares about the
  offset — it checks containment only — but because `arch_task_init_switch_frame()` carves the
  switch frame *below* the trap frame and panics if that lands unaligned
  ([`x86_64/arch.c:264`](../kernel/src/arch/x86_64/arch.c:264)). Copy to
  `kernel_stack_top - sizeof(frame)` exactly as the fabricators do, and both structs fall into
  place.

### Death, in stages

The other half of the Step is the reverse trip, and it does not happen all at once either:

| Stage | Who runs it | What is released |
|---|---|---|
| **exit** | the dying task, in `sys_exit` | its fds — safe, it is not standing on them |
| **still exit** | the dying task, off its own address space | its page tables — only after something else is loaded |
| **switch away** | `sched()` | the CPU. It never comes back. |
| **reap** | *someone else*, in `wait4` or wherever Decision 4 says | the kernel stack it was standing on, and `struct task` |

The rule under the table: **a task cannot free the stack it is executing on, or the page tables
the CPU is walking.** Everything else it can hand back itself. What sits between "switch away" and
"reap" is a task that is not `DEAD` — a zombie — carrying an exit status for a parent that has not
asked yet. Decision 4 is about how long that stage is allowed to last and who ends it.

---

## ⚖️ Decisions — Step-level, all open

### 1. How does `sys_fork` get at the parent's trap frame? ⬜ OPEN

`sys_fork` needs to copy the frame this syscall came in on. It is sitting a few hundred bytes up
the kernel stack, and there is no reliable pointer to it: `current->trap_frame` is written at trap
*exit* ([`sched.c:69`](../kernel/src/proc/sched.c:69)), so mid-syscall it names the frame of
whatever trap came *before* — which, if that was a timer tick taken in kernel mode, was deeper on
this same stack and has since been popped. `task_owns_frame()` would bless the stale pointer, since
it only checks the range.

Three shapes, none of them large:

- **Pass the frame down.** `x86_64_syscall_handler()` and `aarch64_dispatch()` both have it in
  hand and give `syscall_dispatch()` only the registers
  ([`idt.c:214`](../kernel/src/arch/x86_64/idt.c:214),
  [`exceptions.c:100`](../kernel/src/arch/aarch64/exceptions.c:100)). Linux's `sys_fork` is
  `kernel_clone()` reading `current_pt_regs()` — a pointer derived from the stack top, not a
  parameter.
- **Write `current->trap_frame` at entry too**, making the field mean what
  [`task.h:20`](../kernel/include/proc/task.h:20) already says it means.
- **Compute it**: `kernel_stack_top - sizeof(struct trap_frame)`, relying on the invariant above
  that a user-entry frame is always at the top. That is exactly `current_pt_regs()`, and it is
  also an assumption written as arithmetic.

**What settles it:** trace a timer tick landing in the middle of `sys_fork`, on x86_64 where the
syscall path runs with interrupts enabled. Where does `current->trap_frame` point at the moment of
the `memcpy`, and which of the three shapes would have noticed?

### 2. `fork`, or `clone` — what does the ABI say the syscall is? ⬜ OPEN

The Linux ABI commitment ([`SYSCALL_DESIGN.md`](SYSCALL_DESIGN.md) §1-2) means the number a libc
will use, and on AArch64 that number is **`clone` 220 with `flags = SIGCHLD` and everything else
zero** — there is no `fork` entry to give a number to. x86_64 has both, and glibc there calls
`clone` too; musl calls `fork` 57 on x86_64 and `clone` on AArch64.

- **Implement `clone` on both, accepting exactly the `fork` shape**, and reject any other flag with
  `-EINVAL` until threads exist. Alias `fork` 57 to it on x86_64.
- **Implement `fork` under a PrOS-only number on AArch64**, following `SYS_open` 511's precedent,
  and leave `clone` for Phase 9.
- **Something in between** — one handler, two table rows per architecture.

The child-stack, TLS and `parent_tid` arguments of `clone` are all ignorable for the `fork` shape,
so the difference is in which numbers appear in `unistd.h` and which flag-check goes in front of
the handler. `wait4` 61/260 and `exit_group` 231/94 are the other two rows this Step adds;
`exit_group` because that is what a libc's `_exit` actually issues, and it is one line.

**What settles it:** which of these does a statically linked BusyBox on AArch64 issue when its
shell runs a command — and will that number exist in the table on the day Phase 9 tries it?

### 3. What shape is the eager copy? ⬜ OPEN

Phase 7 Decision 4, made concrete. `vmm_copy_context(src, dst)` walks `src`'s lower half, and for
every present leaf allocates a frame, `memcpy`s a page, maps it in `dst`. The VMM has a recursive
walk already — `vmm_free_table_recursive()` ([`vmm.c:77-88`](../kernel/src/mm/vmm.c:77)) — that
does the wrong thing at the leaf. Three things are genuinely undecided:

- **How the flags travel.** There is no way to ask a PTE what abstract flags built it. Either a new
  `arch_vmm_pte_get_flags()` (x86_64 reads bits 1, 2, 63; AArch64 reads `AP[2:1]`, `UXN`/`PXN`,
  `AttrIndx`), or a new `arch_vmm_pte_set_phys()` that keeps the hardware bits and swaps the
  address — copying the PTE rather than re-deriving it. The second is smaller and knows less.
- **Whether the walk is written once or twice.** A `vmm_walk_lower(ctx, fn)` with a per-leaf
  callback would make `destroy` and `copy` two callers of one walk — and is the "shape CoW can grow
  out of" the phase document asked about, since CoW is the same walk with "share and mark
  read-only" at the leaf. Two functions that share nothing is also honest, and the phase document
  said so.
- **What else rides along.** `demand_page_lo/hi` must be copied or the child's stack stops growing
  where the parent's would not. Today that range is 0/0 for every real task, so a bug here is
  invisible until Phase 8 sets it.

Then the thing that makes this Part testable before any process exists: `test_vmm()`
([`test_vmm.c:20-52`](../kernel/src/core/test/test_vmm.c:20)) already has the create / touch /
destroy / "leaks nothing" pattern. A copy test is that pattern with a second context in the middle
— map a writable page and a read-only one, copy, compare through the HHDM, write to one side and
check the other did not move, destroy both, count pages.

**What settles it:** with the PTE-copy shape, what happens to a page whose PTE was never installed
because it is *demand-paged* and not yet touched? And with the flags-readback shape, which
`VMM_*` flag has no honest answer on one of the two architectures?

### 4. Zombies, orphans, and who frees what ⬜ OPEN

Phase 7 Decision 6, plus the half of it that only shows up in this tree. Four coupled questions:

**(a) Is there a `TASK_ZOMBIE`?** If `exit` keeps the task non-`DEAD` until `wait4` collects it, the
exit status has a home and Unix semantics hold. If `exit` goes straight to `DEAD`, the status has
to live somewhere else and "reaped" means something weaker.

**(b) What happens to the last one?** Trace `psh` typing `exit`: `sys_exit` → zombie → `sched()` →
the scheduler loop → `sched_pick_next()` finds nothing → `sched_any_alive()` sees a zombie, which
is not `DEAD` → `arch_idle()`, **forever.** The machine that Phase 6 taught to shut down when the
last program ends now waits for a `wait4` that no parent will ever issue, because `psh`'s parent is
`BOOT`, and `BOOT` is the scheduler thread. This is the phase document's "machine that never shuts
down", with its cause now exact. The options are the orphan policy: reparent to `init` (Unix; but
`init` *is* the thing exiting), let `BOOT` reap in its loop (a scheduler that also does `wait`), or
have a task with no live parent skip the zombie stage entirely.

**(c) Who frees the kernel stack and the `struct task`, and when?** Not the task — see the table
above. `wait4` is the natural reaper for a child with a parent; whoever (b) picks is the reaper for
the rest. And **the address space needs care from both sides**: the exiting task can free it only
after `vmm_switch_context(vmm_kernel_context)`, and a reaper running in the scheduler thread must
remember that the scheduler is still standing on the last task's root table
([`sched.c:144`](../kernel/src/proc/sched.c:144)) — on x86_64 that root page holds the kernel's
own upper-half entries, and freeing it out from under CR3 is a triple fault, not a panic.

**(d) What does `wait4` sleep on, and with which lock?** xv6 sleeps on the parent's own
`struct proc *` as the channel, and `exit` wakes it; `sleep()` here demands a lock it can hand off
([`sched.c:102`](../kernel/src/proc/sched.c:102)), and there is no lock over task state yet. One
`struct spinlock` in `sched.c` covering the ring, parent links and exit status is the smallest
thing that satisfies the signature; whether it is *the* scheduler lock or a `wait_lock` like xv6's
is the naming half of the question. Either way the `while` around the sleep re-scans children,
never `if` — Step 1's rule.

Removing a node from the ring is the mechanical part: a singly-linked ring, `cursor` and
`run_queue_head` possibly pointing at the node, and four walkers that must never see a freed
`next`. O(n) for the predecessor is fine; forgetting `cursor` is a use-after-free that survives
until the next `pmm_alloc` reuses the page.

**What settles it:** trace `psh` → `fork` → child prints and `exit(42)` → parent in `wait4` →
parent `exit` → shutdown, on paper, naming at every arrow which task is running, what state the
other is in, and who has freed what. If any arrow needs a task to touch memory it has already
released, or the last arrow does not reach `arch_shutdown()`, the policy is not done.

### 5. FPU / SIMD state — what this Step actually owes ⬜ OPEN

Phase 7 Decision 5, rewritten against the measurements above. The original framing — "AArch64 is
nearly free, x86_64 needs `-mno-sse` first" — has both halves wrong: the AArch64 kernel executes
SIMD instructions on every `kprintf`, and `-mno-sse` has never reached the compiler. Three
separable questions:

**(a) Kernel hygiene.** Making the kernel *provably* FP-free is a config change:
`-mcpu=x86_64-sse-sse2-mmx+soft_float` / `-mcpu=generic-fp_armv8-neon` in the two
`config.$(ARCH).mk`, `-DPRINTF_DISABLE_SUPPORT_FLOAT` beside them, and the objdump count as the
proof. It removes a class of bug (kernel code silently clobbering a user's `xmm`/`q` registers)
before it exists, and it retires a wrong sentence from two documents. It also means `kprintf`
loses `%f` for good, and that the flags Linux uses are not the flags this toolchain wants —
worth a comment in the `.mk` so the next person does not "fix" it back to `-mno-sse`.

**(b) User FP on x86_64.** Userland *cannot* use SSE today — `CR4.OSFXSR` is clear — and nothing
tries. Turning it on is two bits in `CR4` and then a per-task 512-byte `fxsave` area (16-aligned)
saved and restored on switch, or `CR0.TS` plus a `#NM` handler for the lazy version. Nothing before
Phase 9's libc will issue an SSE instruction, and a libc's `memcpy` will issue one on its first
call.

**(c) User FP on AArch64.** The opposite situation: the FPU is *on* for EL0 and nothing saves it.
Two user tasks share `q0`-`q31`, `fpcr` and `fpsr` by accident. Nothing today uses them, so nothing
today notices. Eager save (528 bytes per task, in `switch_to` or beside the trap frame) or lazy
(`FPEN = 0b00` at EL0, trap, enable, save the previous owner's) are the shapes; a third is to
*turn it off* at EL0 for now, so that a program using NEON faults loudly in Phase 9 instead of
computing with someone else's registers.

Where the save area lives is Step 1's two-frames question again: the FP set is neither caller- nor
callee-saved from the kernel's point of view — the kernel never touches it after (a) — so it is
user state, switched when the *user* changes, which is once per dispatch and not once per trap.

**What settles it:** for each of (b) and (c), name the first program in the roadmap that executes
an FP/SIMD instruction, and say what it observes on that day under each option. Then decide
whether "this Step" is the right answer for something whose first observer is two phases away —
and whether (a), which costs a morning, should wait for that answer at all.

### 6. Does the console get its named owner now? ⬜ OPEN

Phase 7 Decision 3's second half named Step 2 because `fork` is what creates a second holder of
the console's `struct file`. The lock and the `while` around `sleep()` already make two readers
*safe* ([`console_dev.c:71-90`](../kernel/src/drivers/console_dev.c:71)); the `struct tty` and
the owner that answers `-EIO` to a second reader make them *orderly*.

Whether the demo can produce two readers is the question. A parent inside `wait4` is not reading.
A child that only prints is not reading. The scenario that needs an owner is a child reading the
console while its parent is *not* in `wait4` — which is `&`, and `&` is Phase 10.

**What settles it:** write the demo's sequence of syscalls down and mark every `read(0, …)`. If no
two of them can be outstanding at once, the owner is Step 3's problem or Phase 10's, and this
Step's console work is zero. If one can, it is here.

### 7. What is the demo, and who runs it? ⬜ OPEN

`psh` cannot run a program until Step 3, so the program that forks has to be one that already
exists. Two shapes:

- **A `psh` builtin** — `fork` at the prompt: the shell forks, the child prints its pid and
  `exit(42)`s, the parent `wait4`s and prints `child 2 exited 42`, the prompt comes back, `exit`
  shuts the machine down. Demonstrable from the `#` prompt, which the phase document's Verification
  chapter says every Step from here on should be.
- **A separate `/bin/forktest`**, run as `pros.init=` — the `user/Makefile` picks up any
  `src/<name>/` directory and `initrd/Makefile` ships every binary into `/bin`, so a new program is
  a directory and nothing else. Cleaner, and it is not a shell doing something a shell should not
  have to.

Either way it exercises the console under `fork` — both halves write to the same open file — and
either way the parent's `wait4` return value is the only visible proof that reaping happened.
`Ctrl+P` after the child is reaped should show no corpse, and Part C3 wants a free-page count in
that dump so "the pages came back" is something a human can read off the screen.

**What settles it:** the phase document's rule — a feature that cannot be shown at the prompt
should be suspected of not existing — against the cost of a builtin that Step 3 deletes.

---

## 🗺️ Suggested order

```
  C0 ── fields, states, numbers, errno, the two new arch hooks, sys_fork/sys_wait4 as -ENOSYS
   │
  C1 ── vmm_copy_context, proven by a self-test with no process anywhere near it
   │
  B1 ── AArch64: the child's frame, x0 = 0 · and whatever Decision 5 says about q0-q31
  A1 ── x86_64:  the child's frame, rax = 0 · and whatever Decision 5 says about CR4 and fxsave
   │
  C2 ── sys_fork, and the demo's first half: two processes, both print, child still leaks
  C3 ── exit grows a status, wait4, zombies, reaping, orphans — and shutdown still works
  C4 ── the console's named owner, if Decision 6 says so
```

**`C1` before either architecture Part, and before `sys_fork` exists.** The copy is the one piece
that can be proven with nothing else built — a self-test, two contexts, a page count — and it is
also the piece most likely to be subtly wrong (a permission bit dropped, a demand range not
carried). Prove it alone, so that when the child faults in C2 the copy is not a suspect.

**`C2` is the milestone worth pausing on.** Two processes printing is Phase 7's first visible
payoff, and C2 delivers it with the leak still in place: the child dies `DEAD` in the ring exactly
as `init` always has. Everything C3 adds is debugged against a `fork` that already works.

`B1`/`A1` in either order — the frame copy is symmetric. `A1` is the bigger one if Decision 5
turns `CR4` bits on.

---

## 🧩 C0 — Types, numbers, and the skeleton

**Goal:** everything both architecture tracks and both later C Parts need to exist before any of
them can compile.

- `struct task` grows `parent`, an exit status, and whatever Decision 4 (a) adds to the state enum
  — **with its string in `task_state_names[]`**, Step 1's trap, still live.
- `unistd.h`: the rows Decision 2 picks, plus `wait4` and `exit_group` on both architectures.
  `errno.h`: `ECHILD` 10.
- The two arch hooks, declared in `arch.h` beside `arch_task_init_user_frame()`: one that lays a
  copied trap frame at the top of a fresh kernel stack and zeroes its return register, and — only
  if Decision 3 goes that way — one that reads a PTE's flags back or re-targets its address.
- `vmm_copy_context()` declared in `vmm.h`. `sys_fork()`/`sys_clone()` and `sys_wait4()` declared
  in `syscalls.h`, in the table in `syscall.c`, returning `-ENOSYS`.
- If Decision 5 (a) is yes, the `.mk` flags and the `printf` define land here, since they change
  the binary and nothing else in the Step should be debugged on top of an unproven toolchain
  change.

**Verify:** compiles on both architectures. If the flags changed: Phase 6's acceptance run,
unchanged, and the objdump count at zero on both.

---

## 🧩 C1 — `vmm_copy_context()`

**Goal:** a second address space that is byte-for-byte the first, with the same permissions,
proven without a process in sight.

- The walk over `src`'s root entries 0..255, recursively, in the shape Decision 3 chose. At a
  present leaf: `pmm_alloc(1)`, `memcpy` through the HHDM, map into `dst` with the source's
  permissions. Intermediate tables are `dst`'s own — `vmm_map_page()`'s `create` path builds them
  ([`vmm.c:163-174`](../kernel/src/mm/vmm.c:163)).
- `demand_page_lo/hi` copied.
- On failure midway, `dst` is left in a state `vmm_destroy_context()` can clean up — which it can,
  since it frees whatever is present and nothing else.
- The self-test in `test_vmm.c`: a writable page and a read-only page in a fresh context, copy,
  compare both through the HHDM, write the source's writable page and confirm the copy did not
  change, confirm the read-only page's PTE is still read-only in the copy, destroy both, and the
  page count before equals the page count after.

**Verify:** the new `[VMM  ]` lines pass on both architectures. **Take this seriously as the
Step's only isolated proof** — nothing after this Part can test the copy without a scheduler and
two tasks in the way.

---

## 🧩 B1 — AArch64: the child's frame

**Goal:** a kernel stack on which a copied user frame will `eret` correctly.

- Copy `TRAP_FRAME_SIZE` bytes to `kernel_stack_top - TRAP_FRAME_SIZE`, then `x[0] = 0`.
  `vector_type` comes with the copy and is `8`, Lower EL synchronous, so the tail installs the
  frame's `sp` into `SP_EL0` ([`vectors.S:104-109`](../kernel/src/arch/aarch64/vectors.S:104))
  — the child's user stack pointer is the parent's, pointing into the child's own copy of that
  page. `elr` already names the instruction after the `svc`.
- `arch_task_init_switch_frame()` on the result, unchanged from Step 1.
- Decision 5 (c)'s answer, if it is "save": the `q0`-`q31`/`fpcr`/`fpsr` area, where it lives,
  and its copy into the child.

**Verify:** none on its own — C2 is the first thing that runs it. The hook's alignment panic in
`arch_task_init_switch_frame()` is the only assertion between a wrong offset and an `eret` into
garbage.

---

## 🧩 A1 — x86_64: the child's frame

**Goal:** the same, plus whatever Decision 5 turns on.

- Copy `sizeof(struct trap_frame)` to `(kernel_stack_top - sizeof) & ~0xF` — identical to
  `arch_task_init_user_frame()`'s placement — then `rax = 0`. `cs` is `0x3b`, so the unified tail
  `swapgs`es on the way out; `rip` is the parent's `rcx`, the instruction after `syscall`;
  `int_no` stays the `0x80` sentinel, which is honest — the child *did* come from a syscall.
- `arch_task_init_switch_frame()` on the result.
- Decision 5 (b)'s answer, if it is "enable": `CR4.OSFXSR | OSXMMEXCPT` in `arch_init()`, a
  16-aligned 512-byte `fxsave` area per task, `fxsave`/`fxrstor` at the point Decision 5 chose, and
  its copy into the child. If it is "not yet": nothing, and a note in `arch_init()` saying that
  `CR4` is deliberately left as Limine handed it.

**Verify:** as B1.

---

## 🧩 C2 — `sys_fork`, and two processes that print

**Goal:** one syscall, two return values, two address spaces, both alive at once.

- `task_inner_create()` split so a task can be created *without* the console fds — the child
  gets `file_ref()` of each of the parent's `fds[]` instead, 256 slots, `NULL`s included.
- `vmm_create_context()` for the child, `vmm_copy_context()` into it, `parent = current`, the
  parent's `name` pointer, the frame hook from B1/A1, `sched_add_task()`.
- The frame comes from wherever Decision 1 said. The pid goes back through the normal return
  path; the child's `0` was written by the hook.
- The demo's first half, in the vehicle Decision 7 chose: fork, both halves print their pid,
  child `exit`s, parent carries on. No `wait4` yet.

**Verify:** two lines from two pids on both architectures. `Ctrl+P` shows two user tasks, then one
`DEAD` — **the leak is still there, on purpose.** The order the two lines appear in is not
specified: `sched_add_task()` inserts after the head and `cursor` decides who runs next, so do not
write the expected output as if it were.

---

## 🧩 C3 — `exit`, `wait4`, and a reaper

**Goal:** the child is collected, its memory comes back, and the machine still shuts down.

- `sys_exit()` keeps the status, closes its fds, drops its address space *after* switching to the
  kernel context, wakes its parent on the channel Decision 4 (d) chose, applies the orphan policy
  from (b) to its own children, and calls `sched()` in the state (a) picked. It does not touch its
  kernel stack. `sys_exit_group` is the same function.
- `sys_wait4(pid, wstatus, options, rusage)`: under the lock, scan the ring for children; a zombie
  child is unlinked (fixing `run_queue_head` and `cursor`), its status packed Linux-style —
  `(code & 0xff) << 8` — `copy_to_user`'d if `wstatus` is not `NULL`, `task_destroy()`'d; no
  children at all is `-ECHILD`; children but none dead is `sleep()`, in a `while`. `pid == -1`
  and `pid > 0` both; `options` and `rusage` accepted and ignored.
- The reaper for orphans, wherever (b) put it — and the trace from Decision 4 replayed against
  the code, arrow by arrow.
- **An instrument:** `pmm_get_free_page_count()` in the `Ctrl+P` dump, so "fork, wait, count
  returns" is visible from the keyboard.

**Verify:** the demo's second half — the parent prints the child's exit code, both architectures.
`Ctrl+P` after: no corpse, and the free-page count equal to what it was before the `fork`. Then
`exit` at the prompt and **the machine shuts down** — that line is the Step's regression test for
Phase 6, and the one Decision 4 (b) exists for.

---

## 🧩 C4 — The console's named owner

**Goal:** only if Decision 6 says so — `struct tty` gathering the `ldisc`, its lock and its
channel in one allocation the node's `priv_data` points at, and a reader field: the first `read()`
claims it, a second from another task gets `-EIO`, `close` of the last reference releases it.

**Verify:** a second reader gets `-EIO` rather than a line meant for the first; the Phase 6
acceptance run unchanged.

---

## 🕳️ Traps worth knowing about in advance

- **`current->trap_frame` is the previous trap's, not this one's.** Decision 1 exists because of
  it. It is right by coincidence on a task's first syscall and wrong the moment a timer tick has
  landed in the kernel since, which on x86_64 can be *during* `sys_fork` itself.
- **The child's return register is not zero by default.** The handler writes the parent's `rax`/
  `x0` *after* the syscall returns; a copy taken inside it carries `57`, `220` or the first
  argument. Set it in the hook, not "later".
- **A task cannot free its own kernel stack**, and a task cannot free the address space the CPU
  is walking — and on x86_64 the scheduler thread keeps walking the last task's root table until
  the next dispatch, because that root also holds the kernel's upper half. Free it there and the
  symptom is a triple fault with no dump at all.
- **A zombie is alive to `sched_any_alive()`.** The last task exiting into a zombie state that
  nothing reaps is an idle loop with no exit — the machine "hangs" in `wfi`/`hlt` looking exactly
  like it is waiting for a keystroke.
- **Removing a node the `cursor` points at** leaves a dangling `next` that `sched_pick_next()`
  follows on the very next tick. `run_queue_head` the same, one walk later.
- **`task_state_names[]` is indexed by state.** Same trap as Step 1, same consequence: a new
  state without a string makes every dump print garbage for the tasks this Step is about.
- **`task_inner_create()` opens the console for every task.** A fork that uses it unchanged gives
  the child a *different* `struct file` on fd 0/1/2 — a separate offset, a separate reference,
  wrong for every regular file and invisible on the console, which has no offset.
- **`sched_add_task()` inserts after the head.** Which half of the demo prints first depends on
  where `cursor` was, and differs between the two architectures for no reason worth chasing.
- **`wait4`'s status encoding is an ABI.** `WEXITSTATUS` in every libc is `(status >> 8) & 0xff`.
  Store the raw code and Phase 9's `$?` is wrong in a way that looks like a shell bug.
- **`-mno-sse` and `-mgeneral-regs-only` do nothing under `zig cc`.** Adding either "first" is
  adding nothing; the only proof a kernel emits no FP is the objdump count, and the only flags
  that move it are `-mcpu=…`.
- **Enabling `CR4.OSFXSR` without saving state is worse than leaving it off.** Off, a user SSE
  instruction faults on the spot with an `Invalid Opcode` dump. On and unsaved, two programs share
  sixteen registers and the symptom is wrong arithmetic in whichever runs second, in Phase 9.
- **The user stack is one page and nothing grows it.** `demand_page_lo/hi` are `0/0` for every
  task `main.c` builds, and `psh`'s `_start` already reserves 3760 bytes of it on x86_64 and 3824
  on AArch64 (measured: `subq $0xeb0, %rsp` / `sub sp, sp, #0xef0`). A demo that adds a buffer
  to `psh` is spending from what is left of 4 KiB, and the failure is a page fault at a
  lower-half address with no demand range to catch it.
- **The parent's `sleep()` in `wait4` must re-check in a `while`.** Two children, one `wakeup`,
  and the first collect takes the wrong one is the case an `if` gets wrong.
- **`sys_exit` closing fds means `file_unref()` on the console's shared `struct file`** — its
  refcount is what keeps `task_open_std_fds()`'s single open alive across every task; a child that
  unrefs three times on exit takes three of the parent's references with it only if the fork
  forgot to `file_ref()` them. The self-test's `ref_count == 3` line is the shape of the check.
- **Type-ahead is dropped while nobody drains the console.** Step 1's closeout. A child that runs
  for a while with its parent in `wait4` is the first time that window is wide. Not this Step's to
  fix; this Step's to know about when a typed character vanishes.

---

## 📁 Files touched

New:

- ⬜ `user/src/forktest/forktest.c` — only if Decision 7 picks the separate program
- ⬜ the self-test lines in [`test_vmm.c`](../kernel/src/core/test/test_vmm.c) — C1

Existing, to be modified:

- ⬜ [`kernel/include/proc/task.h`](../kernel/include/proc/task.h) — `parent`, exit status, the
  state Decision 4 adds, and the `trap_frame` comment made true (Decision 1)
- ⬜ [`kernel/src/proc/task.c`](../kernel/src/proc/task.c) — `task_state_names[]`,
  `task_inner_create()` split, `sys_exit()` grown up, `sys_fork()`/`sys_wait4()`
- ⬜ [`kernel/src/proc/sched.c`](../kernel/src/proc/sched.c) + `proc/sched.h` — unlink from the
  ring, a lock for `wait4` to sleep on, the orphan reaper if it lives here, the free-page count in
  the dump
- ⬜ [`kernel/src/mm/vmm.c`](../kernel/src/mm/vmm.c) + `mm/vmm.h` — `vmm_copy_context()` and
  the walk shape Decision 3 picks
- ⬜ [`kernel/include/arch/arch.h`](../kernel/include/arch/arch.h) — the fork-frame hook, the PTE
  readback or re-target hook, and any FPU hook from Decision 5
- ⬜ [`kernel/src/arch/x86_64/arch.c`](../kernel/src/arch/x86_64/arch.c) +
  [`aarch64/arch.c`](../kernel/src/arch/aarch64/arch.c) — the hooks above; `CR4` / `CPACR_EL1`
  per Decision 5
- ⬜ [`kernel/src/arch/x86_64/idt.c`](../kernel/src/arch/x86_64/idt.c) +
  [`aarch64/exceptions.c`](../kernel/src/arch/aarch64/exceptions.c) — only if Decision 1 passes
  the frame down or writes it at entry
- ⬜ [`kernel/src/syscall/syscall.c`](../kernel/src/syscall/syscall.c),
  [`kernel/include/asm/unistd.h`](../kernel/include/asm/unistd.h),
  [`kernel/include/core/syscalls.h`](../kernel/include/core/syscalls.h) — the rows Decision 2
  picks, `wait4`, `exit_group`
- ⬜ [`kernel/include/errno.h`](../kernel/include/errno.h) — `ECHILD`
- ⬜ [`kernel/config.x86_64.mk`](../kernel/config.x86_64.mk) +
  [`config.aarch64.mk`](../kernel/config.aarch64.mk) — the `-mcpu=` flags and the `printf`
  define, if Decision 5 (a) is yes; the never-honoured `-mgeneral-regs-only` retired either way
- ⬜ [`kernel/src/drivers/console_dev.c`](../kernel/src/drivers/console_dev.c) — `struct tty` and
  the owner, only if Decision 6 says now
- ⬜ [`user/include/syscall.h`](../user/include/syscall.h) — `sys_fork`/`sys_clone`, `sys_wait4`
  wrappers, hardcoded numbers as always
- ⬜ [`user/src/psh/psh.c`](../user/src/psh/psh.c) — the builtin, if Decision 7 picks it
- ⬜ [`PHASE7_STEP1_BLOCKING.md`](PHASE7_STEP1_BLOCKING.md) and
  [`PHASE7_MANY_PROGRAMS.md`](PHASE7_MANY_PROGRAMS.md) — the `-mgeneral-regs-only` sentence
  corrected in place, the way Phase 6 Step 2's interrupt-state claim was

---

## 🪜 Verification

- **C0** compiles and proves nothing — unless the flags changed, in which case it proves the
  kernel emits no FP, which is the one claim in this Step a human cannot see at the prompt.
- **C1** is the Step's isolated proof: the copy, tested with no process anywhere near it.
- **C2's test is two pids on the screen.** Visible, and the leak is still in place.
- **C3's test is three things at once:** the exit code arrives, the page count returns, and
  `exit` still powers the machine off. The third is Phase 6's payoff, and the one Decision 4 puts
  at risk.
- **Both architectures, every Part**, as always. B1/A1 are where the two differ, and where
  "works on one" says the least — the frame copy is symmetric, but Decision 5's work is not.

When C3 passes on both architectures, **Step 2 is done**: PrOS has two user processes, a parent
that waits without spinning, and a child that is genuinely gone afterward. Step 3 then makes a
child into a *different* program, which is where `psh` finally stops being a shell of builtins.
