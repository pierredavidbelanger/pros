# Working Document: Phase 7 Step 2 — `fork`, `wait4`, Reaping, and FPU State [STATUS: NOT STARTED ⬜]

> [!NOTE]
> **Phase 7 Step 2**, from [`PHASE7_MANY_PROGRAMS.md`](PHASE7_MANY_PROGRAMS.md) Chapters 2 and 3,
> minus `execve`. That document's **Decisions 4, 5 and 6 are still open** and belong here, as does
> the second half of Decision 3 (console arbitration). This document is what those questions look
> like against the tree as [Step 1](PHASE7_STEP1_BLOCKING.md) left it on 2026-08-31.

> [!NOTE]
> Mentor-mode reminder (see the note at the top of [`../README.md`](../README.md)): this is a
> design reference to code from by hand, not code to paste in. **All seven Step-level decisions
> below are resolved** (2026-09-04 and 2026-09-05). Unlike Step 1's first draft, they were
> written open and without recommendation, and each was resolved one exchange at a time, with
> its argument written in the words it was resolved with — the reasoning is the part worth
> re-reading, and it is deliberately not Claude's.

Tracks: **`C`** architecture-neutral · **`A`** x86_64 · **`B`** AArch64.

---

## 🎯 What "done" looks like

> `/bin/init` calls `fork()`. **Two** processes come back from that one call, each with its own
> address space, and both print. `init` calls `wait4()` and **sleeps** — not spins — until the
> child exits, then prints the child's exit code. The child's `struct task`, kernel stack and page
> tables are **freed**, the page count returns to what it was before the `fork`, and when `init`
> itself exits the machine still shuts down.

This is the first time the word *process* means something in PrOS: until now there has been
exactly one user task, built by hand in `main.c`, and the only way a program ended was to leave a
corpse in the run queue for the shutdown check to count. After this Step, programs create programs
and clean up after them.

**What is explicitly *not* in this Step:**

- ❌ **No `execve`.** A child is a copy of its parent and stays one. Step 3 makes it a different
  program, which is why the demo is a program that forks *itself*.
- ❌ **No shell.** `pros.init=` goes back to `/bin/init` for one Step (Decision 7). `psh` keeps
  building and stays in the initrd; Step 3 brings it back as the thing `init` forks and `exec`s.
  There is no prompt in Step 2, and by the Phase 6 rule that is fine — only the phase needs a
  demo, a Step needs to be used by it.
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
| **exit** | the dying task, in `sys_exit` | its fds — safe, it is not standing on them, and Step 4 needs this |
| **switch away** | `sched()` | the CPU. It never comes back. |
| **reap** | its parent, in `wait4` | its page tables, the kernel stack it was standing on, and `struct task` |

The rule under the table: **a task cannot free the stack it is executing on, or the page tables
the CPU is walking.** Everything else it can hand back itself. What sits between "switch away" and
"reap" is a task that is not `DEAD` — a zombie — carrying an exit status for a parent that has not
asked yet. Decision 4 settled how long that stage lasts and who ends it: the parent, always, and
the one task that has no parent skips the stage because the machine is ending.

---

## ⚖️ Decisions — Step-level, all open

### 1. How does `sys_fork` get at the parent's trap frame? ✅ RESOLVED (2026-09-04)

> **Compute it. One arch function, `arch_task_user_frame(kernel_stack_top)`, returns where the
> user-entry frame lives on a given kernel stack — `top - 176` on x86_64, `top - 304` on AArch64.
> The two fabricators become its callers, `sys_clone` its third, and `sched_on_trap_exit()`
> asserts it on every trap from userland.**

In the words it was resolved with: *"because the stack is always fabricated the same way, and
we know the state of the stack we (or the machine) fabricated, so we know what offset to look
at."*

Why the offset is fixed, traced on each entry path: every trap **from userland** starts at the
stack top — `syscall_entry` from `%gs:0`, the IDT path from `tss.rsp0`, both set to
`kernel_stack_top` by `arch_set_kernel_stack()` at dispatch; AArch64 from `SP_EL1`, which the exit
tail's `ldp x0, x1, [sp], #TRAP_FRAME_SIZE` always leaves at the top — and every one of them
pushes the same fixed number of slots. A trap taken *in the kernel* starts wherever `sp` was, and
is never the frame `fork` wants. That is the whole reason the pointer was unreliable and the
arithmetic is not.

The tree already says this twice: `arch_task_init_user_frame()` on both architectures computes
exactly this address to fabricate a first frame, because a fake frame has to sit where a real one
would for the same exit tail to restore it. Naming that arithmetic once, and having the
fabricators and `sys_clone` share it, collapses "the copied frame must land where a real one
would" into a function that cannot disagree with itself. It is Linux's `task_pt_regs()`.

**What the other two shapes could not do:** the function takes a *stack*, not `current`, so it
finds the user-entry frame of **any** task — including one asleep inside `sys_read` on the
console. Step 5 delivers a signal by editing the target's user-entry frame, and the target is
usually blocked. This shape is already that answer.

**The catch, closed.** It is an assumption written as arithmetic: an entry stub that ever pushes
one extra word breaks it silently. So `sched_on_trap_exit()`, which already asserts
`task_owns_frame(current, frame)` on every trap, asserts the stronger thing for a frame that came
from userland (`cs == 0x3b`, `vector_type >= 8`): `frame == arch_task_user_frame(current->kernel_stack_top)`.
Checked on every keystroke on both architectures for the rest of the project, so a stub change
panics on the first trap rather than in `fork` a month later.

**`current->trap_frame` is never read by `fork`.** Its comment in `task.h:20` is rewritten to say
what the field is — written at trap exit, and the fabricated first frame for a task that has
never trapped — rather than made true.

*Kept for the trail — the question as it stood, and the two shapes that lost:* write-at-entry
has the same bug unless the write is conditioned on "from userland" and the exit-side write is
removed, which makes a rule that three entry sites must remember; pass-it-down is structural but
puts a seventh argument on the stack on x86_64 and a gap in the syscall table.

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

### 2. `fork`, or `clone` — what does the ABI say the syscall is? ✅ RESOLVED (2026-09-04)

> **The `open`/`openat` precedent, unchanged. The call that exists on both architectures is the
> implementation: `sys_clone`, at 56 and 220. The other delegates: `sys_fork` is
> `sys_clone(SIGCHLD, 0, 0, 0, 0)`, at Linux's 57 on x86_64 and at an invented PrOS-only number
> on AArch64, exactly as `SYS_open` 511 already is.**

In the words it was resolved with: *"we do the same as `open`/`openat`: one call is the
implementation (the one that exists on both platforms), the other just delegates; we invent a
number for the call that doesn't exist in the Linux ABI."*

What that fixes in place:

- `sys_clone(flags, stack, parent_tid, …)` accepts **exactly** `flags == SIGCHLD` (17) with every
  other argument zero, and answers anything else with `-EINVAL`. That is the `fork` shape and the
  only one this Step builds; threads are the day the check is relaxed.
- The invented number has to be below `NR_SYSCALLS` (512) and above anything Linux uses; 511 is
  taken by `open`. The user-side wrapper in `user/include/syscall.h` hardcodes it the way
  `sys_open` hardcodes 511 — with the same comment pointing at `unistd.h`.
- **The argument order of `clone` differs between the two architectures in Linux**: x86_64 is
  `(flags, stack, parent_tid, child_tid, tls)`, the generic table AArch64 uses is
  `(flags, stack, parent_tid, tls, child_tid)`. Invisible for the `fork` shape, where everything
  past `flags` is zero — and exactly the kind of thing worth a comment on the handler so the
  thread work does not learn it the hard way.
- `wait4` 61/260 and `exit_group` 231/94 are the other rows, no precedent needed: both exist on
  both.

*Kept for the trail — the question as it stood:*

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

### 3. What shape is the eager copy? ✅ RESOLVED (2026-09-05)

> **The obvious one: create a context, walk the parent's lower half, allocate and copy each
> present page, map it in the child with the parent's permissions, copy the demand range. The
> permissions are *decoded* out of the PTE by a new `arch_vmm_pte_get_flags()`, the inverse of
> `arch_vmm_make_pte()`. A second plain recursive walk, sharing nothing with `destroy`.**

In the words it was resolved with: *"allocate a new context, allocate pages, copy them, then walk
the parent and copy permissions — and that's it"*; and on the one real question inside it,
*"easier to just hand a pointer with everything correct in it, but it seems more correct to
decode the permissions."*

```
vmm_copy_context(src, dst):
    for each present leaf in src's lower half:
        phys = pmm_alloc(1)
        memcpy(hhdm(phys), hhdm(leaf's phys), PAGE_SIZE)
        vmm_map_page(dst, virt, phys, arch_vmm_pte_get_flags(*leaf))
    dst->demand_page_lo/hi = src's
```

Twenty lines. `vmm_create_context()` already gives the empty `dst`, `vmm_map_page()` already
builds `dst`'s intermediate tables on demand, and `vmm_free_table_recursive()` already shows what
the descent looks like — this is that loop with a different body at the leaf.

**The decoder.** `uint64_t arch_vmm_pte_get_flags(uint64_t pte)` in `arch.h` beside the three
`arch_vmm_pte_*` functions that exist, reading back the bits `make_pte` wrote. x86_64 is a straight
mirror: bits 0, 1, 2, 4, 63. AArch64 has two that are not straight and want a comment each:
`WRITABLE` is `AP[2]` (bit 7) **clear** — the bit means read-only — and `NO_EXECUTE` went in as
two bits, `UXN` (54) and `PXN` (53), so the decoder reads one, `UXN`, and says why one is enough.
Hardware-managed bits (accessed/dirty on x86_64, the access flag on AArch64) are not flags and
are not read. The raw-copy alternative — `arch_vmm_pte_set_phys()` keeping every hardware bit,
xv6's `PTE_FLAGS(*pte)` — was rejected as knowing less: CoW will need to *read* a flag before it
decides, and a decoder is a read ability the VMM has never had.

**How it is known to be right.** A decoder has one property, that it undoes `make_pte`, and it
can be tested with no page table at all: all 32 flag combinations through `make_pte` then
`get_flags`, expecting `flags | VMM_PRESENT` back (`vmm_map_page()` always sets `PRESENT`). One
loop in `test_vmm()`, both architectures, pins the inversion and the two-bit `NO_EXECUTE` shut
before the copy test in C1 sends anything through a real tree.

**Left as the honest default, not decided:** one walk or two. A second plain recursive function,
as the phase document allowed. The visitor that `destroy` and `copy` could share is the shape CoW
would grow out of, and it can be factored on the day CoW needs it — CoW being the same walk with
"share and mark read-only" at the leaf.

*Kept for the trail — the question as it stood:*

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

### 4. Zombies, orphans, and who frees what ✅ RESOLVED (2026-09-04)

> **Unix's answer, with `init` as a real `init`. `wait4` is the only path that frees a task.
> Orphans are reparented to pid 1, whose job is to wait for them. A task with no live parent —
> which can only be `init` itself, at the end — goes straight to `DEAD` and is never freed, because
> the machine is shutting down.**

In the words it was resolved with:

> *"`init` is our reaper process, he will do the wait that permits to reap tasks that are orphaned
> (and reparented to him). `init` becomes the entry point again: he will fork, the child prints and
> exits, the parent will wait for the child to exit, then exit himself; the sched sees there is no
> more task alive, we shut down."*

The four rules that sentence contains, spelled out:

- **Exit with a live parent → `TASK_ZOMBIE`, `wakeup(parent)`.** The task keeps its `struct task`
  and its kernel stack, carrying the exit status for a parent that has not asked yet. Before that,
  it closes its own fds — safe, it is not standing on them, and **Step 4 depends on it**: a pipe's
  reader gets `EOF` when the writer's end *closes*, which has to happen at `exit`, not at reap, or
  `ls | cat` hangs until someone waits.
- **Exit with no live parent → `TASK_DEAD`, freed by nobody.** Only `init` can be in this position
  — its parent is `NULL`, since `BOOT` is the scheduler thread and not in the ring — and it is in
  it exactly once, at power-off. One `struct task` and one kernel stack leak at the moment
  `sched_any_alive()` says no and `arch_shutdown()` runs. Not a leak in any sense that matters.
  **This is the rule the shutdown payoff rests on:** had `init`'s exit become a zombie, the loop
  would idle forever waiting for a `wait4` nobody can issue.
- **`wait4` is the one reaper.** It collects the status, unlinks the zombie from the ring, and
  calls `task_destroy()` — page tables, stack, struct, in one function that already exists. It runs
  on the *parent's* stack with the *parent's* root loaded, so nothing it frees is in use. The CR3
  hazard named above never arises: nothing frees page tables from the scheduler thread.
- **Exit reparents its live children to pid 1** before becoming a zombie — walk the ring, every
  task whose `parent` is the exiting one gets `init` instead — so their later `exit` wakes a parent
  that is actually waiting. If `init` is the one exiting, its children get `NULL`, and the rule
  above takes them when their turn comes.

The channel a parent sleeps on in `wait4` is its own `struct task *`, xv6's shape; `exit` wakes
it. The lock `sleep()` demands is one `struct spinlock` in `sched.c`, taken by `exit`, `wait4`
and the reparenting walk, covering child state, exit status, parent links and the ring. Around
the sleep, `while`, never `if`: two children, one `wakeup`, and the parent must collect one and
go back for the other.

**Why this and not the scheduler-as-reaper shape.** It was the other candidate: `wait4` only
collects, the scheduler loop frees `DEAD` tasks after they switch away, which is Linux's
`finish_task_switch()`. It also gives one reaper path, but it makes the scheduler thread touch
process memory, needs it to switch off the dead task's root first, and puts the reparenting walk
in kernel code that no `wait4` ever calls. Unix's shape needs a real `init`, and this tree can
have one now: `init` forks, the child does the work, the parent waits — which is Step 3's `init`
minus one `exec`. Nothing written for it is thrown away.

**A parent blocked somewhere other than `wait4`** — on the console, say — leaves its dead child a
zombie until it gets around to waiting. That is what a zombie is for, and it is correct.

*Kept for the trail — the four questions as they stood, and the trace that decided them:*

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

### 5. FPU / SIMD state — what this Step actually owes ✅ RESOLVED (2026-09-04)

> **All three, now. (a) The kernel becomes provably FP-free in C0, with the flags that this
> toolchain actually honours and a comment saying why. (b) and (c) are eager save and restore at
> dispatch, in C: two arch functions, `arch_fpu_save(task)` and `arch_fpu_restore(task)`, called
> from `scheduler()` around the switch, with the state area living in `struct task`.**

Phase 7 Decision 5, rewritten against the measurements above. The original framing — "AArch64 is
nearly free, x86_64 needs `-mno-sse` first" — had both halves wrong: the AArch64 kernel executes
SIMD instructions on every `kprintf`, and `-mno-sse` has never reached the compiler. It was three
separable questions:

**(a) Kernel hygiene.** Making the kernel *provably* FP-free is a config change:
`-mcpu=x86_64-sse-sse2-mmx+soft_float` / `-mcpu=generic-fp_armv8-neon` in the two
`config.$(ARCH).mk`, `-DPRINTF_DISABLE_SUPPORT_FLOAT` beside them, and the objdump count as the
proof. It removes a class of bug (kernel code silently clobbering a user's `xmm`/`q` registers)
before it exists, and it retires a wrong sentence from two documents. It also means `kprintf`
loses `%f` for good, and that the flags Linux uses are not the flags this toolchain wants —
hence the comment in the `.mk`, so the next person does not "fix" it back to `-mno-sse`. **This is
what makes (b) and (c) small:** once the kernel never touches the FP registers, they are user
state and only need switching when the *user* changes — once per dispatch, not once per trap.
That is Linux's rule too; kernel code that wants the FPU there has to ask with
`kernel_fpu_begin()`.

**(b) User FP on x86_64.** Userland *cannot* use SSE today — `CR4.OSFXSR` is clear — and nothing
tries. Turning it on is two bits in `CR4` and then a per-task 512-byte `fxsave` area, 16-aligned,
saved and restored at dispatch.

**(c) User FP on AArch64.** The opposite situation: the FPU is *on* for EL0 and nothing saves it.
Two user tasks share `q0`-`q31`, `fpcr` and `fpsr` by accident. Nothing today uses them, so
nothing today notices. 528 bytes per task, saved and restored at dispatch.

#### Why now, and why AArch64 is the dangerous one

The first program in the roadmap to execute an FP/SIMD instruction is the static hello world of
Phase 9 Step 1 — not for a float, but because a libc's `memcpy`, `strlen` and `memset` are
written with SSE2 and NEON, and a C runtime copies and zeroes things before `main`. Under
"defer", the two architectures fail in opposite ways on that day, and the difference is the whole
argument, in the words it was resolved with:

> *"Not saved registers on x86_64 are not used and would throw an exception if used, so no
> mysterious behaviour. But on AArch64, those not saved registers ARE enabled, so they will not
> throw any exception if used — they are not used now, so we don't see the mysterious behaviour
> now."*

Spelled out: `struct trap_frame` saves `x0`-`x30`, `struct switch_frame` saves `x19`-`x30`, and
`q0`-`q31`/`fpcr`/`fpsr` are in neither — the only user-visible registers nothing in the tree ever
saves. On x86_64 that is harmless, because `CR4` makes every SSE instruction a `#UD` with a dump
naming the task and the `RIP`: found in five minutes on the first run. On AArch64,
`CPACR_EL1.FPEN = 0b11` lets two user tasks use the registers freely, so A loads sixteen bytes
into `q0` inside `memcpy`, the timer fires, B runs its own `memcpy` through `q0`, A resumes and
**stores B's bytes into A's buffer.** No fault, no dump, only when the tick lands inside a copy —
which, two phases from now, looks like a libc bug or a race in BusyBox rather than a scheduler
that does not know the FPU exists. "Enabled but unsaved" is the configuration that lies;
"disabled" merely does not work yet. Both are unfinished, and only one of them is a heisenbug.

So Step 2 is the right moment: exactly two processes, a `Ctrl+P`, and nothing else in the way.
The demo carries the proof — both halves of the fork run a loop of `double` arithmetic and check
their own result, no inline asm needed, since the user build keeps SSE/NEON on and at `-O0` a
`volatile double` goes through `xmm0`/`d0` on every iteration.

#### Why eager, not lazy

Lazy needs a trap handler on each architecture — `CR0.TS` plus `#NM` on x86_64, `FPEN = 0b00`
plus its trap on AArch64 — an owner pointer, and a rule for what happens when the owner exits.
Eager is two small asm helpers and three C lines in `scheduler()`, at the place Step 1 already
put `arch_set_kernel_stack()` and `vmm_switch_context()`: restore `next` before
`arch_task_switch_to()`, save it after control comes back while `current` is still `next`. Linux
dropped lazy FPU switching on x86 in 2016 because eager turned out to be both faster and
simpler. It is also the choice that keeps everything that decides anything in C.

**The area is a field of `struct task`**, `aligned(16)` — `kmalloc` already guarantees
`HEAP_ALIGNMENT 16` ([`heap.h:7`](../kernel/include/mm/heap.h:7)) — and `fork` copies it with the
rest of the struct. Cost: 512 + 528 bytes per task, and a few dozen cycles per dispatch at 100 Hz.

#### Traps this resolution brings with it

- **A zeroed `fxsave` area is not a valid initial state.** `fxrstor` of zeros sets `MXCSR` to 0,
  unmasking every SSE exception, so a fresh task's first float division raises `#XM`. A new
  task's area needs `MXCSR = 0x1F80` and the x87 control word at `0x37F` — Linux's
  `fpstate_init`. AArch64 has no such problem: `fpcr = 0` *is* the default.
- **After (a), the assembler runs under the same `-mcpu` as the compiler.** The AArch64 save
  routine names `q` registers in a kernel built without NEON, so it needs an `.arch_extension`
  directive or its own `.S` file with `.arch armv8-a+fp+simd`. On x86_64, `fxsave` is its own
  feature (`fxsr`) and stays available.
- **The x86_64 QEMU target has no `-cpu`**, so it runs `qemu64`, which has no AVX. `fxsave` covers
  everything the guest can use today. The day someone passes `-cpu host`, the upper halves of the
  `ymm` registers are not saved and `fxsave` is silently wrong — the AArch64 failure, imported.
  One comment where `CR4` is set.

### 6. Does the console get its named owner now? ✅ RESOLVED (2026-09-04) — not this Step

> **No. Nobody reads the console in Step 2 at all** (Decision 7: `init` is the only program and it
> never calls `read`), so there is nothing to arbitrate. It comes back with `psh` in Step 3 — and
> even then `init` sits in `wait4` and never reads, so there is still exactly one reader. The
> owner is owed by whichever Step first has two readers outstanding at once.

*Kept for the trail — the question as it stood:*

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

### 7. What is the demo, and who runs it? ✅ RESOLVED (2026-09-04)

> **`/bin/init`, as `pros.init=` again, and it is the reaper of Decision 4.** It forks; the child
> prints its pid and `exit(42)`s; `init` loops on `wait4(-1, …)`, prints `child 2 exited 42`,
> gets `-ECHILD`, exits; the machine shuts down. `psh` is not the entry point for one Step and is
> not modified. The child also forks a grandchild and exits *first*, so the reparenting walk is
> code a run has actually executed — without that, the orphan rule is never exercised. Both
> halves run the `volatile double` loop from Decision 5 too. This bends the phase document's
> "demonstrable from the prompt" rule for exactly one Step, and Step 3 puts the prompt back on
> top of this same `init`.

*Kept for the trail — the two shapes as they stood:*

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
  B1 ── AArch64: the child's frame, x0 = 0 · arch_fpu_save/restore over q0-q31, fpcr, fpsr
  A1 ── x86_64:  the child's frame, rax = 0 · CR4 on, arch_fpu_save/restore over the fxsave area
   │
  C2 ── sys_fork, and init's first half: two processes, both print, child still leaks
  C3 ── exit grows a status, wait4, zombies, reaping, orphans — and shutdown still works
```

**`C1` before either architecture Part, and before `sys_fork` exists.** The copy is the one piece
that can be proven with nothing else built — a self-test, two contexts, a page count — and it is
also the piece most likely to be subtly wrong (a permission bit dropped, a demand range not
carried). Prove it alone, so that when the child faults in C2 the copy is not a suspect.

**`C2` is the milestone worth pausing on.** Two processes printing is Phase 7's first visible
payoff, and C2 delivers it with the leak still in place: the child dies `DEAD` in the ring exactly
as `init` always has. Everything C3 adds is debugged against a `fork` that already works.

`B1`/`A1` in either order — the frame copy is symmetric. `A1` is the bigger one, since it also
turns `CR4` bits on and has to hand a fresh task a *valid* FPU state, not a zeroed one.

---

## 🧩 C0 — Types, numbers, and the skeleton

**Goal:** everything both architecture tracks and both later C Parts need to exist before any of
them can compile.

- `struct task` grows `parent`, an exit status, the FPU state area (Decision 5 — per
  architecture, `aligned(16)`, its size and layout in each arch's own header), and
  `TASK_ZOMBIE` (Decision 4) — **with its string in `task_state_names[]`**, Step 1's trap, still
  live.
- `unistd.h`: `clone` 56/220, `fork` 57 and its invented AArch64 number, `wait4` 61/260,
  `exit_group` 231/94 (Decision 2). `errno.h`: `ECHILD` 10.
- The arch hooks, declared in `arch.h` beside `arch_task_init_user_frame()`:
  `arch_task_user_frame(kernel_stack_top)` (Decision 1), with both fabricators rewritten as its
  callers; one that lays a copied trap frame there and zeroes its return register;
  `arch_fpu_save(task)` / `arch_fpu_restore(task)` and whatever initialises a fresh task's area
  (Decision 5); and `arch_vmm_pte_get_flags(pte)`, the inverse of `arch_vmm_make_pte()`
  (Decision 3), with its 32-combination round-trip test in `test_vmm()` landing with it.
- The assertion in `sched_on_trap_exit()`: a frame from userland is at
  `arch_task_user_frame(current->kernel_stack_top)`, or panic (Decision 1). Lands here, before
  anything depends on it, so Phase 6's acceptance run proves the invariant on both architectures.
- `vmm_copy_context()` declared in `vmm.h`. `sys_clone()`, `sys_fork()` delegating to it, and
  `sys_wait4()` declared in `syscalls.h`, in the table in `syscall.c`, returning `-ENOSYS`.
- **The kernel goes FP-free here** (Decision 5 (a)): `-mcpu=x86_64-sse-sse2-mmx+soft_float` and
  `-mcpu=generic-fp_armv8-neon` in the two `.mk`, the never-honoured `-mgeneral-regs-only`
  retired, `-DPRINTF_DISABLE_SUPPORT_FLOAT` beside them, and a comment saying why these and not
  `-mno-sse`. It lands first because it changes the binary, and nothing else in the Step should be
  debugged on top of an unproven toolchain change.

**Verify:** compiles on both architectures. Phase 6's acceptance run, unchanged, and the objdump
count of FP/SIMD instructions at **zero** on both — that count is the only proof (a) has.

---

## 🧩 C1 — `vmm_copy_context()`

**Goal:** a second address space that is byte-for-byte the first, with the same permissions,
proven without a process in sight.

- The walk over `src`'s root entries 0..255, recursively — a second plain function beside
  `vmm_free_table_recursive()`, sharing nothing with it (Decision 3). At a present leaf:
  `pmm_alloc(1)`, `memcpy` through the HHDM, `vmm_map_page()` into `dst` with
  `arch_vmm_pte_get_flags(*leaf)`. Intermediate tables are `dst`'s own — `vmm_map_page()`'s
  `create` path builds them ([`vmm.c:163-174`](../kernel/src/mm/vmm.c:163)).
- A refusal up front: `src` is never `vmm_kernel_context`. Nothing that forks owns it, and its
  lower half is Limine's on one architecture and a placeholder on the other.
- `demand_page_lo/hi` copied.
- On failure midway, `dst` is left in a state `vmm_destroy_context()` can clean up — which it can,
  since it frees whatever is present and nothing else.
- The self-test in `test_vmm.c`: a writable page and a read-only page in a fresh context, copy,
  compare both through the HHDM, write the source's writable page and confirm the copy did not
  change, confirm the read-only page's PTE still decodes as read-only in the copy, destroy both,
  and the page count before equals the page count after.

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
- `arch_fpu_save()` / `arch_fpu_restore()` over `q0`-`q31`, `fpcr` and `fpsr` — sixteen
  `stp`/`ldp` pairs and two `mrs`/`msr`, in a `.S` beside `switch.S` or under an
  `.arch_extension`, since the C side is now built without NEON. A fresh task's area zeroed is
  correct here.

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
- `CR4.OSFXSR | OSXMMEXCPT` in `arch_init()`, with the `qemu64`/AVX comment beside it.
  `arch_fpu_save()` / `arch_fpu_restore()` as `fxsave`/`fxrstor` over the 512-byte area. **A fresh
  task's area is not zeroed:** `MXCSR = 0x1F80`, x87 control word `0x37F`, the rest zero — or the
  first user float division dies of an unmasked exception.

**Verify:** as B1.

---

## 🧩 C2 — `sys_fork`, and two processes that print

**Goal:** one syscall, two return values, two address spaces, both alive at once.

- `task_inner_create()` split so a task can be created *without* the console fds — the child
  gets `file_ref()` of each of the parent's `fds[]` instead, 256 slots, `NULL`s included.
- `vmm_create_context()` for the child, `vmm_copy_context()` into it, `parent = current`, the
  parent's `name` pointer, the frame hook from B1/A1, `sched_add_task()`.
- The parent's frame is `arch_task_user_frame(current->kernel_stack_top)` (Decision 1) —
  `current->trap_frame` is never read. The pid goes back through the normal return path; the
  child's `0` was written by the hook.
- `scheduler()` grows the two FPU calls around `arch_task_switch_to()` — restore `next` before,
  save it after — beside `arch_set_kernel_stack()` and `vmm_switch_context()`, which is where the
  things that ride along with a switch already live.
- `init`'s first half (Decision 7): `pros.init=/bin/init` in `limine.conf`, `init.c` forks, both
  halves print their pid, the child `exit`s, `init` carries on and exits. No `wait4` yet. Both
  halves also run a loop of `volatile double` arithmetic and check their own result — the FPU
  proof, in C, with no inline asm.

**Verify:** two lines from two pids on both architectures. `Ctrl+P` shows two user tasks, then one
`DEAD` — **the leak is still there, on purpose.** The order the two lines appear in is not
specified: `sched_add_task()` inserts after the head and `cursor` decides who runs next, so do not
write the expected output as if it were.

---

## 🧩 C3 — `exit`, `wait4`, and a reaper

**Goal:** the child is collected, its memory comes back, and the machine still shuts down.

- The lock: one `struct spinlock` in `sched.c` over child state, exit status, parent links and
  the ring, plus `sched_remove_task()` — the one place that unlinks, fixing `run_queue_head` and
  `cursor`.
- `sys_exit()` keeps the status, closes its fds, hands its live children to pid 1 (Decision 4),
  then: live parent → `TASK_ZOMBIE` and `wakeup(parent)`; none → `TASK_DEAD`. Then `sched()`. It
  frees nothing else — not its address space, not its stack. `sys_exit_group` is the same
  function.
- `sys_wait4(pid, wstatus, options, rusage)`: under the lock, scan the ring for children; a zombie
  child has its status packed Linux-style — `(code & 0xff) << 8` — `copy_to_user`'d if `wstatus`
  is not `NULL`, is unlinked, and is `task_destroy()`'d; no children at all is `-ECHILD`; children
  but none dead is `sleep()` on our own `struct task *`, in a `while`. `pid == -1` and `pid > 0`
  both; `options` and `rusage` accepted and ignored.
- `init`'s second half: the `wait4` loop, the printed exit code, the grandchild that gets
  orphaned and collected, and `init`'s own exit — the trace from Decision 4 replayed against the
  code, arrow by arrow.
- **An instrument:** `pmm_get_free_page_count()` in the `Ctrl+P` dump, so "fork, wait, count
  returns" is visible from the keyboard.

**Verify:** `init` prints the child's exit code and the grandchild's, both architectures.
`Ctrl+P` between the collect and `init`'s exit: no corpse, and the free-page count equal to what
it was before the first `fork`. Then `init` exits and **the machine shuts down** — that line is
the Step's regression test for Phase 6, and the one Decision 4's no-live-parent rule exists for.

---

## 🕳️ Traps worth knowing about in advance

- **`current->trap_frame` is the previous trap's, not this one's.** Decision 1 exists because of
  it. It is right by coincidence on a task's first syscall and wrong the moment a timer tick has
  landed in the kernel since, which on x86_64 can be *during* `sys_fork` itself. The resolution
  never reads it; the trap is kept for whoever is tempted to.
- **The child's return register is not zero by default.** The handler writes the parent's `rax`/
  `x0` *after* the syscall returns; a copy taken inside it carries `57`, `220` or the first
  argument. Set it in the hook, not "later".
- **A task cannot free its own kernel stack**, and a task cannot free the address space the CPU
  is walking — and on x86_64 the scheduler thread keeps walking the last task's root table until
  the next dispatch, because that root also holds the kernel's upper half. Free it there and the
  symptom is a triple fault with no dump at all. Decision 4 routes around both: `exit` frees
  neither, `wait4` frees both from the parent's stack with the parent's root loaded. The trap is
  kept because the day someone "optimises" `exit` to free its own page tables, this is the bug.
- **A zombie is alive to `sched_any_alive()`.** The last task exiting into a zombie state that
  nothing reaps is an idle loop with no exit — the machine "hangs" in `wfi`/`hlt` looking exactly
  like it is waiting for a keystroke. Decision 4's no-live-parent rule is the fix; get the "is my
  parent alive" test wrong — a parent that is itself a zombie counts as *not* alive — and this is
  what it looks like.
- **`exit` must close its fds itself, not leave them to `wait4`.** Invisible in this Step, fatal
  in Step 4: a pipe reader's `EOF` is the writer's end *closing*, and a child that keeps its fds
  until reaped keeps `ls | cat` from ever finishing.
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

- ⬜ the self-test lines in [`test_vmm.c`](../kernel/src/core/test/test_vmm.c) — C1

Existing, to be modified:

- ⬜ [`kernel/include/proc/task.h`](../kernel/include/proc/task.h) — `parent`, exit status,
  `TASK_ZOMBIE`, the FPU area, and the `trap_frame` comment rewritten to say what the field is
  (Decision 1)
- ⬜ [`kernel/src/proc/task.c`](../kernel/src/proc/task.c) — `task_state_names[]`,
  `task_inner_create()` split, `sys_exit()` grown up, `sys_fork()`/`sys_wait4()`
- ⬜ [`kernel/src/proc/sched.c`](../kernel/src/proc/sched.c) + `proc/sched.h` —
  `sched_remove_task()`, the lock `exit`/`wait4` share, the two FPU calls in `scheduler()`, the
  user-frame assertion in `sched_on_trap_exit()`, the free-page count in the dump
- ⬜ [`kernel/src/mm/vmm.c`](../kernel/src/mm/vmm.c) + `mm/vmm.h` — `vmm_copy_context()`, a
  second plain recursive walk (Decision 3)
- ⬜ [`kernel/include/arch/arch.h`](../kernel/include/arch/arch.h) — `arch_task_user_frame()`
  (Decision 1), the fork-frame hook, `arch_vmm_pte_get_flags()` (Decision 3),
  `arch_fpu_save()` / `arch_fpu_restore()` and the fresh-area initialiser (Decision 5)
- ⬜ [`kernel/src/arch/x86_64/arch.c`](../kernel/src/arch/x86_64/arch.c) +
  [`aarch64/arch.c`](../kernel/src/arch/aarch64/arch.c) — the hooks above; `CR4` bits on
  x86_64, and a per-arch header naming the FPU area's size and layout
- ⬜ [`kernel/src/syscall/syscall.c`](../kernel/src/syscall/syscall.c),
  [`kernel/include/asm/unistd.h`](../kernel/include/asm/unistd.h),
  [`kernel/include/core/syscalls.h`](../kernel/include/core/syscalls.h) — `clone`, `fork`
  delegating to it, `wait4`, `exit_group` (Decision 2)
- ⬜ [`kernel/include/errno.h`](../kernel/include/errno.h) — `ECHILD`
- ⬜ [`kernel/config.x86_64.mk`](../kernel/config.x86_64.mk) +
  [`config.aarch64.mk`](../kernel/config.aarch64.mk) — the `-mcpu=` flags and the `printf`
  define, the never-honoured `-mgeneral-regs-only` retired, and the comment saying why (Decision 5)
- ⬜ `kernel/src/arch/aarch64/fpu.S` or an `.arch_extension` block — the only place in the
  kernel allowed to name a `q` register
- ⬜ [`user/include/syscall.h`](../user/include/syscall.h) — `sys_fork`/`sys_clone`, `sys_wait4`
  wrappers, hardcoded numbers as always
- ⬜ [`user/src/init/init.c`](../user/src/init/init.c) — the demo and the reaper: fork, a
  grandchild, the `wait4` loop (Decision 7)
- ⬜ [`limine.conf`](../limine.conf) — `pros.init=/bin/init` on both architectures, for one Step
- ⬜ [`PHASE7_STEP1_BLOCKING.md`](PHASE7_STEP1_BLOCKING.md) and
  [`PHASE7_MANY_PROGRAMS.md`](PHASE7_MANY_PROGRAMS.md) — the `-mgeneral-regs-only` sentence
  corrected in place, the way Phase 6 Step 2's interrupt-state claim was

---

## 🪜 Verification

- **C0** compiles and proves nothing — unless the flags changed, in which case it proves the
  kernel emits no FP, which is the one claim in this Step a human cannot see at the prompt.
- **C1** is the Step's isolated proof: the copy, tested with no process anywhere near it.
- **C2's test is two pids on the screen.** Visible, and the leak is still in place.
- **C3's test is three things at once:** the exit codes arrive — the child's and the orphaned
  grandchild's — the page count returns, and `init`'s exit still powers the machine off. The
  third is Phase 6's payoff, and the one Decision 4's last rule exists for.
- **Both architectures, every Part**, as always. B1/A1 are where the two differ, and where
  "works on one" says the least — the frame copy is symmetric, but Decision 5's work is not.

When C3 passes on both architectures, **Step 2 is done**: PrOS has a real `init`, two user
processes under it, a parent that waits without spinning, and a child that is genuinely gone
afterward. Step 3 then makes a child into a *different* program — `init` forks and `exec`s
`/bin/psh`, the prompt comes back, and `psh` finally stops being a shell of builtins.
