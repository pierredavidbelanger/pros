# Working Document: Phase 7 Step 1 — `switch_to`, a Scheduler Thread, and Blocking [STATUS: NOT STARTED ⬜]

> [!NOTE]
> **Phase 7 Step 1**, from [`PHASE7_MANY_PROGRAMS.md`](PHASE7_MANY_PROGRAMS.md) Chapter 1. That
> chapter's **Decisions 1 and 2 are already resolved** — a real `switch_to`, xv6's dedicated
> scheduler thread with `BOOT` filling the role, and xv6-style channels for `sleep`/`wakeup`.
> This document is what those resolutions mean against the actual tree.

> [!NOTE]
> Mentor-mode reminder (see the note at the top of [`../README.md`](../README.md)): this is a
> design reference to code from by hand, not code to paste in. **All six Step-level decisions
> below are resolved** (2026-08-26). Three landed somewhere other than where their recommendation
> pointed — 1b's attribution was wrong, 2 grew, 3 shrank — and the reasoning is kept in each,
> because the reasoning is the part worth re-reading.

Tracks: **`C`** architecture-neutral · **`A`** x86_64 · **`B`** AArch64.

---

## 🎯 What "done" looks like

> A task that calls `read()` on `/dev/console` and finds nothing there **stops running** — it is
> not on the run queue, it burns no cycles, and the UART interrupt is what puts it back. The
> machine with nothing to do sits in `wfi`/`hlt` instead of spinning through a loop whose entire
> job is to notice that nothing has happened.

**This Step adds no user-visible feature.** Typing at the prompt works exactly as it did the day
Phase 6 closed, and that is the point: the payoff is an *absence*, which is why Part C2 below
spends as much effort on how to see it as on how to build it.

**What is explicitly *not* in this Step:**

- ❌ **No `fork`.** There is still exactly one user task. `sleep`/`wakeup` gets built and exercised
  with one sleeper, which is the honest scope — Step 2 is where a second one appears.
- ❌ **No console arbitration.** The other half of Phase 7 Decision 3 — the `struct tty`, the
  lock, the named owner — belongs to Step 2, because nothing can create a second reader yet. One
  console reader stays an assumption here, and stays true.
- ❌ **No timeouts, no `poll`.** Phase 7 Decision 2 named `poll` as what eventually retires
  channels. Nothing here should make that migration harder, and nothing here should start it.
- ❌ **No reaping.** Dead tasks still accumulate. Step 2 owns that, together with what it does to
  shutdown.

---

## 🧠 Where this actually stands

Confirmed by reading the tree, not assumed:

- **The trap-exit path already switches kernel stacks.** `x86_64_exception_handler()` and
  `x86_64_syscall_handler()` (`idt.c:206`, `idt.c:214`) and AArch64's `exceptions.c:127` all end
  in `return sched_on_trap_exit(frame)`, and both stubs then do `movq %rax, %rsp` / `mov sp, x0` —
  **the returned frame pointer becomes the stack pointer.** That is a kernel-stack switch, and it
  already works. It is only safe at trap exit because the frame is the sole thing on the incoming
  stack that matters at that instant. `switch_to` is the general version of a trick already in
  the tree, not a new idea.
- **Three trap-exit sites, not two.** The x86_64 syscall path is separate from the IDT path on
  purpose (`idt.c:212`'s comment: a syscall has no interrupt number, so `int_no` stays meaningful
  in every other dump). Both call `sched_on_trap_exit`.
- **`x86_64_syscall_handler()` enables interrupts across `syscall_dispatch()`** and disables them
  again before `sched_on_trap_exit()`. So a task blocking *inside* a syscall calls into the
  scheduler with **interrupts on** — see Decision 3 and the traps.
- **`sched_on_trap_exit()` already handles "there is no current task"** — `if (!current) return
  frame;`, written for traps that arrive before the scheduler exists. That check is exactly what
  a running scheduler thread needs, and it generalizes for free.
- **`sched.c` holds `run_queue_head`, `current` and `need_resched`**, all `static`. `sched_add_task()`
  special-cases the first task: it sets `current`, calls `arch_set_kernel_stack()` and marks it
  `TASK_RUNNING` by hand, "because `sched_on_trap_exit` has not scheduled it by itself yet".
- **`sched_on_trap_exit()` does three things on a switch**: `arch_set_kernel_stack(current->kernel_stack_top)`,
  `vmm_switch_context(current->ctx)`, and the guard checks `task_stack_intact()` on both sides.
  All three have to move to wherever the switch ends up.
- **`BOOT` has no `kernel_stack_base`.** `task_init_boot()` passes `arch_get_stack_pointer()` as
  the stack top, so `task_owns_frame()` and `task_stack_intact()` both short-circuit to `true`
  for it, and `task_dump()` prints a special "boot stack, extent unknown, guard n/a" line. It runs
  on Limine's stack and always has.
- **`task_dump_all()` walks the `next` ring from `sched_get_current_task()`.** If `BOOT` leaves
  the ring, `BOOT` stops being dumped — silently.
- **A new kernel task is a fabricated trap frame.** `arch_task_init_frame()` writes a frame with
  `rip = entry` and stashes `task_exit_guard` in the slot a stray `ret` would pop. There is no
  saved *callee* state anywhere, because nothing has ever needed one.
- **`struct spinlock` is irqsave-only** — `spinlock_lock_irqsave()` returns flags,
  `spinlock_unlock_irqrestore()` puts them back. There is deliberately no plain `spin_lock()`.
- **`console_dev_ops_read()` spins** on `ldisc_ready()`, popping from `console_input_pop()` itself.
  The comment already says why it is sound: interrupts stay on and a timer tick preempts the loop.
- **AArch64 saves no FP registers anywhere**, because `-mgeneral-regs-only` means the kernel emits
  none. `switch_to` on that side has no `d8`-`d15` to preserve — the same flag that makes Phase 7
  Decision 5 nearly free makes this Part smaller too.

---

## 🧵 Two frames, not one

The single most useful thing to have straight before writing any of this: **a task ends up with
two saved-state structures, and they are not redundant.**

`struct trap_frame` is not "the user's state" — kernel threads get trap frames too. `BOOT` and
anything from `task_create()` are interrupted by the timer like anything else, and a frame is
pushed with `cs = KERNEL_CODE`. The AArch64 vectors already turn on exactly this: the
`cmp x2, #8` / "Current EL vs Lower EL" branch exists *because* a frame can come from either side.

The axis that actually separates them is **involuntary vs voluntary**:

| | `trap_frame` | `switch_frame` |
|---|---|---|
| **Built by** | hardware plus the trap stub | a normal C call to `arch_switch_to` |
| **Cause** | the task was *stopped* | the task *stepped aside* |
| **Saves** | everything (~22 slots) | callee-saved only (~6) |
| **Why that size** | the victim never agreed to lose a register | the compiler already spilled what mattered |

**The register-set difference is the definition, not an optimization.** If they were the same
concept they would hold the same set.

### Why one pointer has been enough until now

Because a task has only ever paused at **trap exit**, where its kernel stack holds nothing but the
trap frame. One pointer saves everything. A *blocked* task pauses somewhere else entirely:

```
high  ┌──────────────────────┐  <- kstack_get_top
      │ trap_frame           │  pushed by the trap stub on kernel entry
      ├──────────────────────┤
      │ syscall_dispatch     │
      │ sys_read             │   the live C call chain —
      │ console_dev_ops_read │   this is what is new
      │ sleep                │
      ├──────────────────────┤
      │ switch_frame         │  pushed by arch_switch_to
low   └──────────────────────┘  <- the task's saved kernel sp
```

The trap frame is still there and still correct — it still says where to go when the syscall
eventually finishes. But it says **nothing about where in the kernel we were**. Restoring it from
inside `sleep()` would return to userland immediately and throw away the half-finished `read()`.

So the trap frame is the **destination**; the switch frame is the **bookmark**. They sit at
opposite ends of the same kernel stack with the live call chain between them, and at trap exit
that middle section is empty and the two are adjacent — which is exactly why one pointer has
covered both cases so far.

### The invariant this Step establishes

```
scheduler picks you ──> switch_frame ──> kernel C code unwinds ──> trap_frame ──> userland
```

**The scheduler always resumes a task through its `switch_frame`. A task always leaves the kernel
through its `trap_frame`.** Every resume passes through both, in that order. Anything that appears
to skip one is a bug worth chasing immediately.

---

## ⚖️ Decisions — Step-level, all resolved (2026-08-26)

### 1. What are the fields called? ✅ RESOLVED (2026-08-26)

> **Mirror the type name on all three, finishing a convention `struct task` already follows — and
> take the rename now, while it is 13 lines.**

```c
struct task {
    uint64_t pid;
    int state;
    char const *name;
    void *kernel_stack_base;
    void *kernel_stack_top;
    struct trap_frame *trap_frame;      // how this task returns to where it was interrupted
    struct switch_frame *switch_frame;  // how the scheduler resumes it
    struct vmm_context *vmm_context;    // its address space
    uint64_t switch_count;
    struct file *fds[MAX_OPEN_FILES];
    struct task *next;
};
```

**This is not a new naming scheme, it is the one the struct already has.** `frame` and `ctx` are
the *only* abbreviated fields in it — `kernel_stack_base`, `kernel_stack_top`, `switch_count`,
`state`, `name` are all spelled out. The two oldest fields simply never got the treatment the rest
did, and adding a second frame is what makes that stop being harmless.

Three reasons beyond consistency:

- **Never ambiguous at a call site.** `task->frame` requires knowing which kind. `task->trap_frame`
  does not, and after this Step there genuinely are two.
- **Greppable.** `switch_frame` finds the type, the field, the arch code and this document.
  `frame` finds half the kernel.
- **A collision avoided.** A field named `context` next to `struct vmm_context *ctx` would put two
  "contexts" in one struct meaning saved registers and address space respectively. Nobody in the
  field uses the word twice: Linux pairs `thread` with `mm`, BSD pairs `pcb` with `vmspace`. xv6
  *does* call it `context` — but xv6's address-space field is `pagetable`, so it has no collision
  to avoid. (AArch64 Linux does have a `struct cpu_context`, which is the same idea under a name
  that is already taken here.)

**Resist `address_space` for `ctx`**, tempting as it reads. The tree's word for that concept is
already *context* — `vmm_create_context`, `vmm_switch_context`, `vmm_current_context`. A field
called `address_space` would introduce a second vocabulary for one idea, which is worse than a
long name.

**Cost, measured:** 5 uses of `->frame` and 9 of `->ctx` — 14 occurrences on 13 lines, across
`sched.c`, `task.c` and `test_task.c`. (`fb.c` matches the grep but is an unrelated
`->framebuffers`; an earlier count here included it.) It never gets cheaper than this.

**And write the validity windows down while renaming**, because `frame`'s current
`// valid only while suspended` stops being precise the moment there are two of them and they are
valid at different times. Those two comments are half the value of doing this at all.

That comment is in fact **already wrong**, before this Step changes anything: `sched_on_trap_exit()`
writes `current->frame = frame` on *every* trap, switch or not, so it is valid whenever the task is
in the kernel, not only while suspended. The accurate pair after the rename: `trap_frame` is where
this task re-enters the world it was interrupted from, written on every kernel entry; `switch_frame`
is where the scheduler resumes it, valid **only** while it is not running.

### 1b. What shape is the `switch_frame` field? ✅ RESOLVED (2026-08-26)

> **A pointer to a frame on the task's own kernel stack, mirroring `trap_frame`. The struct type
> exists to *name a layout*, not to hold anything.**

- **By value** — `struct switch_frame switch_frame;` inline. Simple, no pointer to get wrong, and
  `struct task` is already large (`fds[256]` alone is 2 KiB). **This is what xv6 actually does** —
  `struct proc` embeds `struct context`, and `swtch` stores `ra`/`sp`/`s0`-`s11` into it.
- **By pointer** — at a frame pushed onto the task's kernel stack. **This is Linux's shape**, not
  xv6's: `__switch_to_asm` pushes the callee-saved set onto the outgoing stack and saves only
  `%rsp` into `thread.sp`. It is also what `trap_frame` already does here.

*(An earlier draft of this document credited the pointer shape to xv6. It is the other way round,
and the mix-up is worth recording, because the two shapes disagree about where the registers
physically live.)*

The pointer, for two reasons:

- **Consistency.** This tree already has one "pointer to saved state on the task's own stack", and
  a second of the same shape beats two conventions side by side.
- **It can be asserted.** State that lives on the stack can be checked against it —
  `kstack_contains()`, exactly as `task_owns_frame()` already does for trap frames. State embedded
  in `struct task` cannot be.

**The consequence worth being explicit about: `struct task` grows by one pointer, not by seven
words.** The registers live on the kernel stack; nothing in C ever allocates a `switch_frame`. The
type is a lens over bytes the assembly wrote — the same relationship `struct trap_frame` already
has with `isr_common_stub`, down to the field order being the **reverse of the push order**,
because the last push lands at the lowest address and a struct reads low-to-high.

So the type earns its keep for exactly one job: fabricating the first frame for a brand-new task,
in C, in `task_create()`. Linux does the same and nothing more — `thread.sp` is a bare
`unsigned long`, and `struct inactive_task_frame` exists solely so `copy_thread()` can write the
initial one with named fields.

**On x86_64 the return address is a struct field and must be the last one** — highest address,
popped last by `ret`. AArch64 puts it in `x30`, a field like any other. That asymmetry is the whole
of Decision 2's remaining risk, and making it a named field rather than a hand-computed offset is
most of the defence. `aarch64/trap_frame.h`'s `TRAP_FRAME_OFF_*` plus `_Static_assert` pattern is
the tool already in the tree for keeping a C struct and its assembly honest.

### 2. Where does the first switch into a brand-new task land? ✅ RESOLVED (2026-08-26)

> **A trampoline, xv6's `forkret` by another name — reading `current->trap_frame` rather than
> inferring it, and landing in *one* exported trap-return tail per architecture.**

This is the fiddly part of the whole Step. `switch_to` ends in a `ret` that goes to whatever return
address the incoming `switch_frame` carries. A task that has run before has a real one. A
**brand-new**
task has never called `switch_to`, so its return address has to be fabricated — and it must land
somewhere that knows how to get to the task's trap frame and restore it.

```
switch_to(scheduler, new_task)
    → ret lands in task_trampoline()
        → arch_trap_return(current->trap_frame)
            → iretq / eret into the entry point
```

The two architectures make this look different, and the difference is worth naming before writing
either one:

- **AArch64 has a link register.** `x30` is the return address, so the fabricated `switch_frame` sets it
  explicitly. What happens is written down in a register.
- **x86_64 has no link register.** `ret` pops the return address *off the stack*, so the fabricated
  `switch_frame` has to place the trampoline's address at exactly the right stack slot. What happens is
  implied by an offset, and getting it wrong jumps to garbage rather than failing an assertion.

`arch_task_init_frame()` already does something in this family — it stashes `task_exit_guard` in
the slot a stray `ret` would find. That precedent is the shape to follow.

#### The alternative that was rejected, and why it was tempting

A trampoline is not strictly necessary *today*. Lay the fabricated `switch_frame` immediately below
the trap frame and the arithmetic comes out to nothing: on x86_64 the six pops plus `ret` consume
exactly `sizeof(struct switch_frame)`, so `sp` lands on the trap frame by construction and the
return address can simply be a label in the existing restore tail. AArch64 is the same identity at
96 bytes. Zero new code, and the whole "fabricate a landing site" problem evaporates.

**It fails the moment there is more than one CPU** — and that is not a distant concern, it is why
both reference kernels have a landing pad at all:

```
CPU0                                  CPU1 (scheduler)
----                                  ----------------
A->state = READY;
                                      picks A: it is READY
switch_to(&A->switch_frame, sched)    switch_to(sched, A->switch_frame)   <- stale bookmark
  ^ has not written the bookmark yet    and CPU0 is still executing on A's kernel stack
```

No ordering of those two lines fixes it. The state change and the bookmark write have to be atomic
with respect to the other CPU, which means a lock held from before the state change until after the
switch, released by whoever comes out the other side. A resumed task releases it where `sched()`
returns; a **brand-new** task has no C frame to release it from. That is the entire reason xv6 has
`forkret`, and Linux `ret_from_fork` → `schedule_tail()` → `finish_task_switch()`. Nobody in the
field lands a new task straight in the restore tail.

**`fork` alone does not force this** — `fork` never switches, it builds a child and enqueues it.
SMP does.

#### What the trampoline buys immediately

Because it reads `sched_get_current_task()->trap_frame` instead of inferring the frame from where
`sp` happened to land, **the adjacency requirement disappears** — and with it most of the hazard
named above. The `switch_frame` no longer has to sit at a computed offset below the trap frame; the
only fabricated value left is a return address that is a linker symbol.

Two costs, both small, both worth knowing before writing it:

- **Alignment.** `ret` lands with `rsp ≡ 0 (mod 16)`; SysV wants `≡ 8` at a C function's entry —
  a `call` would have pushed eight bytes and `ret` does not. So the fabricated `switch_frame` needs
  eight bytes of padding as a **named field**, or the first `movaps` the compiler emits inside the
  trampoline faults. AArch64 has no equivalent requirement.
- **One extra jump**, executed exactly once per task ever created.

#### One tail, not three

`arch_trap_return()` needs a single exported restore tail to jump to, and building it uncovers
something already wrong. **x86_64 has two full copies of the tail** — `isr.S:89-106` and
`syscall_entry.S:47-71` — and they are not the same code:

```asm
    addq $16, %rsp
    cmpq $0x3b, 8(%rsp)     /* syscall_entry.S only */
    jne 1f
    swapgs
1:  iretq
```

`swapgs` appears **only** in `syscall_entry.S`; `isr.S` never touches GS. A `swapgs` on entry is
paired with a `swapgs` on exit *through the same path* — but after a context switch, the frame you
leave through does not belong to the trap you entered through. Task A enters via `syscall` (GS =
kernel), is preempted, and task B leaves via `isr.S`'s tail into ring 3 with the kernel GS base
still installed; B's next `syscall` does `swapgs`, gets the user base, and `movq %gs:0, %rsp` loads
a stack pointer from near address zero.

It is unreachable today only because there is exactly one user task, so the far side of every
switch is `BOOT`, which is kernel-mode and never dereferences `%gs`. **Step 2 lights the fuse** —
`fork` is precisely "now there are two user tasks". See the traps below.

**And this bug has already been fixed once, on the other architecture.** `vectors.S` says it out
loud:

> *"Which stack pointer `->sp` goes back to is a property of the frame being returned to, not of
> the trap that got us here. After a context switch those are two different tasks, possibly from
> different exception levels."*

`swapgs` is that sentence with `SP_EL0` replaced by `GS_BASE`. AArch64 reads the answer out of the
frame; x86_64 reads it out of the path.

So: factor the x86_64 tail into one exported `x86_64_trap_return`, keep the conditional `swapgs`
(correct on both paths), and have both stubs and `arch_trap_return()` reach it. On AArch64 there is
one entry path, so it is only a `.global` label placed **after** `mov sp, x0` — after, so the
`vector_type` branch still runs. Aiming at `.Lrestore_gprs` directly would send a new *user* task's
`sp` into the wrong bank.

#### The invariant to write down

*Nothing is held across `switch_to` yet. When something is, the trampoline is where a brand-new
task releases it, and `sched()`'s return is where a resumed task does.* That sentence is the reason
the trampoline exists at all with an empty body.

Naming, following `task_exit_guard` — the tree's other fabricated address a task can arrive at:
`task_trampoline` in `task.c`, `arch_trap_return()` in `arch.h` beside `arch_task_init_frame()`.

### 3. What is `sleep()`'s signature? ✅ RESOLVED (2026-08-26)

> **`void sleep(void *chan, struct spinlock *lk)` — xv6's, unchanged. The irqsave flags do not need
> threading through it, and the change this Step really forces is in `spinlock.h`, not here.**

The lock argument is not decoration; it is the fix for the **lost wakeup**. Without it:

```
    reader                          UART ISR
    ------                          --------
    if (!ldisc_ready(ld))
                                    byte arrives, line completes
                                    wakeup(chan)   <- nobody is asleep yet
    sleep(chan)                     ...
    (sleeps forever)
```

`sleep(chan, lk)` closes the window by taking the lock *before* checking the condition and
releasing it only once the task is committed to sleeping. That is precisely why the API takes a
lock it did not acquire, which looks wrong until you have seen this race.

#### Why the flags do not need threading through

The complication looked like this: `struct spinlock` is irqsave-only, so the flags live in a
`uint64_t` the caller holds, not in the lock. Surely `sleep()` has to take them, or hand fresh ones
back?

No — and seeing why is the useful part. **`flags` is not "the CPU's interrupt state". It is the
state *this task* had before *this task* took the lock** — a fact about one task's own nesting
history, produced and consumed by the same task. And because this tree passes flags **by value in
a local**, that local lives on the sleeping task's kernel stack and **rides across the switch for
free**.

Which is exactly what xv6 needs a dedicated hack for, because it keeps the same intent in a
*per-CPU* variable and has to hand-carry it around the switch:

```c
intena = mycpu()->intena;
swtch(&p->context, &mycpu()->context);
mycpu()->intena = intena;
```

The `_irqsave`-returns-the-flags convention, chosen in Phase 6 Step 2 before there was anything to
switch, turns out to have already solved this.

```c
// release lk and block on chan, reacquire lk on the way out
void sleep(void *chan, struct spinlock *lk) {
    struct task *task = sched_get_current_task();
    task->chan = chan;
    task->state = TASK_BLOCKED;
    spinlock_unlock(lk);   // plain: hand off the lock, keep irqs masked until we are off the cpu
    sched();               // we return here, later, on our own stack
    task->chan = NULL;
    // discard: these flags describe the scheduler, our caller still holds our own
    (void)spinlock_lock_irqsave(lk);
}
```

That discard is the subtlety, and it is right in both directions. The re-acquire only has to
establish "masked and held" — the opinion about what comes *after* is already sitting in the
caller's local:

| caller's state before its `lock_irqsave` | its `flags` | after its `unlock_irqrestore(flags)` |
|---|---|---|
| interrupts on (the syscall path) | on | on ✓ |
| interrupts off (nested) | off | off ✓ |

#### What this does force: splitting the spinlock primitive

`spinlock_unlock_irqrestore()` cannot be what `sleep` calls, because it re-enables interrupts
**while the task is still `TASK_BLOCKED` and still on the CPU** — reopening the exact window
`sleep(chan, lk)` exists to close. The UART ISR fires there, `wakeup()` marks a task `TASK_READY`
that has not switched away yet, and `sched()` runs anyway.

The mask has to stay down from before the condition test until after the switch. `spinlock.c` shows
the two halves are already there, just not separable:

```c
uint64_t spinlock_lock_irqsave(struct spinlock *lock) {
    uint64_t flags = arch_irq_save();   // the irq half
    while (__atomic_exchange_n(&lock->locked, 1, __ATOMIC_ACQUIRE)) {   // the mutual exclusion half
```

So `spinlock_lock`/`spinlock_unlock` become real, and `_irqsave`/`_irqrestore` become their
composition with the mask. The header's existing comment is there to stop callers forgetting the
mask, and it still should be — reworded to name the one legitimate exception: *take the `_irqsave`
pair unless you are `sleep()`, which needs the two halves to happen at different times.*

Then add the assertion that catches every future misuse, at the top of `sched()`:

```c
if (arch_irq_enabled()) kpanic("sched() with interrupts enabled");
```

That is xv6's `panic("sched interruptible")`, and it turns the whole class of "somebody blocked
without masking" from a shell that hangs once a week into an immediate panic. `arch_irq_enabled()`
does not exist yet — `arch_irq_save()` returns opaque flags and nothing can ask the plain question.

#### Which lock, concretely

The condition is `ldisc_ready(&data->ld)`, so the channel is `&data->ld` — and **the `ldisc` has no
lock today**, because only the reader task has ever touched it. C2 moves feeding into the UART ISR,
which makes it ISR-shared for the first time, so `struct console_dev_node_data` grows a
`struct spinlock` beside its `ld`:

```c
uint64_t flags = spinlock_lock_irqsave(&data->lock);
while (!ldisc_ready(ld)) sleep(ld, &data->lock);
int64_t n = ldisc_drain(ld, buffer, size);
spinlock_unlock_irqrestore(&data->lock, flags);
```

`while`, never `if`.

### 4. Does the scheduler thread stay a `struct task`? ✅ RESOLVED (2026-08-26)

> **Yes — `BOOT` stays a `struct task`, and simply stops being in the run queue.**

It needs a kernel stack (it has one: Limine's), it should keep appearing in `task_dump()`, and
`switch_to` needs somewhere to save *its own* `switch_frame` when it switches away. Making it a `struct task`
that is merely not enqueued costs nothing and keeps every existing tool working.

**The consequence is worse than "`BOOT` stops being dumped", and has to be handled in the same
breath.** `task_dump_all()` opens with:

```c
struct task *first = sched_get_current_task();
if (!first) return;
```

and while the scheduler thread runs there *is* no current task — `current` is `NULL`, xv6's
`c->proc = 0`. So the dump does not quietly lose one line, **it returns immediately and prints
nothing**. Its three callers are `idt.c:200`, `exceptions.c:49` and `sched.c:51` — *all* panic
paths. The failure mode is a fault taken inside the scheduler producing a panic with no task dump
at all, during the Step whose entire new code is the scheduler loop and whose idle branch is fresh
assembly.

`run_queue_head` is already `static` in `sched.c` and, after `sched_add_task()`, nothing ever reads
it again. Anchor the walk there instead of on `current`, and that field gets a job.

While moving it: `task_dump_all()` living in `task.c` and reaching back through
`sched_get_current_task()` is backwards — `sched.c` owns the ring. The walk belongs in `sched.c`
(dump the scheduler thread explicitly, then the ring); `task_dump()` of a single task stays in
`task.c`. Three call sites.

**Deliberately deferred:** the scheduler thread could be given a real `kstack_alloc()` stack instead
of Limine's, entered by a one-way `switch_to` that never returns, putting a guard value under what
is about to become the most-executed stack in the system — the one stack `task_stack_intact()`
returns `true` for unconditionally. It is cheap. It also changes what `task_dump()` prints for
`BOOT` and adds a second unproven thing to the Part where the switch itself is unproven. **Not in
this Step** — recorded so that "how would we ever catch a scheduler stack overflow" has an answer
on file instead of being asked again in Phase 8.

### 5. What replaces `sched_only_current_is_alive()`? ✅ RESOLVED (2026-08-26)

> **Nothing — the scheduler loop asks the question directly.**

```
scheduler():
    forever:
        pick a READY task; if there is one, switch to it, loop
        no READY task, but some task is still alive  -> idle (wfi/hlt), loop
        no live task at all                          -> shut down
```

The predicate is absorbed rather than rewritten (Phase 7 Chapter 1), and Decision 4 makes it
*simpler* than it is today: `sched_only_current_is_alive()` needs its awkward `if (task != first …)`
self-exclusion only because the asker is itself inside the ring. Once the scheduler leaves the ring,
the question is just **"does the ring contain a non-`DEAD` task"** — no exclusion, no special case.

`main.c:166-171` collapses to `scheduler();`, with the "All done here, shutting down." `kprintf` and
`arch_shutdown()` moving into its terminating branch.

**The real work is the idle instruction**, and the two architectures earn the same guarantee by
different mechanisms:

- **x86_64 — `sti; hlt`, adjacent, in that order.** `sti` does not unmask until *after the following
  instruction*: a one-instruction shadow that exists precisely so this pair cannot race. Put
  anything between them and a wakeup arriving in the gap is delivered before the `hlt`, which then
  sleeps with the work already pending.
- **AArch64 — `wfi` wakes on a pending physical interrupt even with `DAIF.I` set.** The mask governs
  *delivery*, not wake-up, so the ordering hazard does not exist on that side at all.

Neither is `arch_halt()`, which masks interrupts and loops forever — idling on it means never waking
up.

**One behaviour to name out loud:** after C2, "alive" includes `TASK_BLOCKED`. A system where every
task is blocked and nothing can ever wake them **idles forever rather than shutting down**. That is
correct, not a bug — the loop has no way to tell a deadlock from a machine waiting on a keystroke.
Written here so it is not rediscovered later as one.

---

## 🗺️ Suggested order

```
  C0 ── struct switch_frame, TASK_BLOCKED, the arch_switch_to declaration, scheduler skeleton
   │
  B1 ── AArch64 switch_to + the new-task trampoline      (do this one first)
  A1 ── x86_64 switch_to + the new-task trampoline
   │
  C1 ── sched.c restructured: BOOT is the scheduler thread, sched_on_trap_exit becomes a caller
  C2 ── sleep/wakeup, and the console spin retired
```

**`B1` before `A1`, contrary to the usual habit.** AArch64 has a link register, so "where does the
first switch land" is a value written into `x30` — explicit, and wrong in a way that is easy to
see. x86_64 expresses the same thing as a value at a stack offset, which is wrong in a way that
jumps to garbage. Build the version that can be read first, then port it to the version that has
to be counted.

**`C1` is the milestone worth pausing on**, because it lands `switch_to` and the scheduler thread
with **no new behaviour at all** — the test is that Phase 6's demo still works, unchanged. Every
later Part is debugged through a scheduler that has already been proven.

---

## 🧩 C0 — Types and the skeleton

**Goal:** everything that both architecture tracks need to exist before either can compile.

- **The field rename first** (Decision 1) — `frame` → `trap_frame`, `ctx` → `vmm_context`, and
  the new `switch_frame` alongside them. Doing it before anything else means every later Part is
  written against the final names, and the 13 lines never grow.
- `struct switch_frame` — the callee-saved set, per architecture, in each arch's own header.
  **x86_64 SysV:** `rbx`, `rbp`, `r12`-`r15`, then **eight bytes of named padding**, then the
  return address `ret` will pop — last field, highest address (Decisions 1b and 2).
  **AArch64 AAPCS64:** `x19`-`x28`, `x29` (fp), `x30` (lr), and no padding question. No `d8`-`d15`
  on either — the kernel emits no FP.
- `TASK_BLOCKED` in the state enum, and a matching entry in `task_state_names[]` — **it is a plain
  array indexed by state, so a new state without a new string prints garbage.**
- `void *chan` in `struct task` — what a `TASK_BLOCKED` task is sleeping on, `NULL` otherwise.
- `void arch_switch_to(struct switch_frame **old, struct switch_frame *new);` in `arch/arch.h`,
  next to the existing `arch_task_init_frame()` declarations — plus `arch_trap_return()`,
  `arch_idle()` and `arch_irq_enabled()` (Decisions 2, 5 and 3).
- **The spinlock split** (Decision 3): expose `spinlock_lock`/`spinlock_unlock` as the mutual
  exclusion half, recast the `_irqsave` pair as their composition with the mask, and reword the
  header comment to name `sleep()` as the sole caller of the bare pair.
- The `sched()` / `scheduler()` skeleton in `sched.c`, not yet called by anything, with the
  `arch_irq_enabled()` assertion already in it.

**Verify:** compiles on both architectures. Nothing runs.

---

## 🧩 B1 — AArch64 `switch_to`

**Goal:** two kernel tasks switch to each other by swapping kernel stacks, with the trap-exit path
untouched.

- The assembly: store `x19`-`x30` at the outgoing frame, write the new `sp` through `old`, load
  the incoming one, `ret`. It belongs beside `vectors.S`, not inside it — it is not a vector.
- `arch_task_init_switch_frame()` building a `switch_frame` whose `x30` points at
  `task_trampoline` (Decision 2).
- `arch_trap_return()`: `mov sp, x0`, then branch to a new `.global` label in `vectors.S` placed
  **after** the existing `mov sp, x0` and **before** the `vector_type` test, so the
  `SP_EL0`/`SP_EL1` decision still runs. **`vectors.S` already contains that sequence**, including
  the `VECTOR_ENTRY`/`.Lrestore_gprs` split. Reuse it rather than writing a second one — two copies
  of that logic drifting apart is a bug that only shows up when a user task is involved, and
  aiming at `.Lrestore_gprs` directly would send a new user task's `sp` into the wrong bank.

**Verify:** a throwaway second kernel task created with `task_create()`, switched to and from by
hand from `main()` before the timer is enabled, each printing. No scheduler involved yet — this
Part is proving the register save/restore and the trampoline, nothing else.

---

## 🧩 A1 — x86_64 `switch_to`

**Goal:** the same, on the architecture where the return address is an offset rather than a
register.

- Push `rbx`/`rbp`/`r12`-`r15`, `movq %rsp, (%rdi)`, `movq %rsi, %rsp`, pop, `ret`.
- The fabricated `switch_frame` must place `task_trampoline`'s address exactly where that final
  `ret` will look, with the eight padding bytes below it so the trampoline is entered at
  `rsp ≡ 8 (mod 16)` as SysV requires (Decision 2). **This is the one place in the Step where being
  off by eight bytes jumps to an address nobody chose**, with no assertion in between — mitigated,
  not removed, by making both slots named struct fields instead of computed offsets.
- **Unify the restore tail here** (Decision 2). `isr.S:89-106` and `syscall_entry.S:47-71` are two
  full copies that differ: only the syscall one does the conditional `swapgs`. Factor the syscall
  version into one exported `x86_64_trap_return`, have both stubs `jmp` to it, and let
  `arch_trap_return()` — `movq %rdi, %rsp; jmp x86_64_trap_return` — be its third caller. **This
  fixes a latent bug, not just duplication**; see the traps below.
- `-mno-red-zone` is already in `config.x86_64.mk`, so the 128 bytes below `rsp` are not a hazard
  here. Worth knowing rather than rediscovering.

**Verify:** the same throwaway two-task switch as B1, byte-identical output.

---

## 🧩 C1 — The scheduler thread

**Goal:** `switch_to` becomes the only way tasks switch, `BOOT` becomes the scheduler, and
**nothing observable changes.**

- `sched_on_trap_exit()` stops swapping frames. On `need_resched` it calls `sched()`, which
  `switch_to`s to the scheduler thread; when the task is chosen again it *returns* from that call
  and hands back the frame it was given. **The `movq %rax, %rsp` in the stubs stays** — it just
  always restores the same frame now.
- The three things that ride along with a switch move to the scheduler loop:
  `arch_set_kernel_stack()`, `vmm_switch_context()`, and both `task_stack_intact()` guard checks.
- `sched_add_task()` loses its first-task special case — the scheduler loop does that work now.
- `BOOT` leaves the run queue. `main()` stops spinning on `sched_only_current_is_alive()` and falls
  into `scheduler()`, which never returns. The shutdown call moves inside it (Decision 5).
- **While the scheduler thread runs there is no current task.** `sched_on_trap_exit()`'s existing
  `if (!current) return frame;` covers a timer tick that lands there — confirm it does rather than
  assume, because `task_owns_frame()` panics on the path just below it.
- `task_dump_all()`'s walk moves into `sched.c`, anchors on `run_queue_head` instead of `current`,
  and dumps the scheduler thread explicitly (Decision 4). **Do this before trusting any panic in
  this Part** — with `current` NULL inside the scheduler, the pre-move version prints nothing at
  all from the three panic paths that call it.

**Verify:** **Phase 6's acceptance run, unchanged** — boot to `#`, `ls /`, `cat /root/lorem.txt`,
`exit`, machine shuts down. Both architectures. If anything about that output differs, this Part
is not done. `switch_count` in `task_dump_all()` should still climb for the user task.

---

## 🧩 C2 — `sleep`, `wakeup`, and a console that waits

**Goal:** the spin is gone, and the machine idles.

- `sleep(void *chan, struct spinlock *lk)` and `wakeup(void *chan)` (Decision 3). `wakeup` walks
  every task marking `TASK_BLOCKED` sleepers on that channel `TASK_READY`.
- `struct console_dev_node_data` grows a `struct spinlock` beside its `ld`: the `ldisc` becomes
  ISR-shared for the first time in this Part, and that is the lock `sleep()` is handed.
- **Write the migration rule where the functions are declared** (Phase 7 Decision 2): callers use
  only these two, never the internals, because the process-table scan is what `poll` replaces
  later without touching a single caller.
- `console_dev_ops_read()` sleeps on its `ldisc` instead of spinning. The waker is the side that
  completes a line — which today is *the reader itself*, since `console_dev_ops_read()` is what
  calls `console_input_pop()` and `ldisc_feed()`. **That has to move**: if nobody drains the ring
  buffer while the reader sleeps, the line never completes and the wakeup never fires. Feeding the
  `ldisc` belongs in the UART ISR path, and `wakeup(ld)` fires there when `ldisc_ready()` turns
  true.
- The idle branch: enable interrupts, halt until one arrives. **Not `arch_halt()`** — see
  Decision 5.
- **Every sleeper re-checks its condition in a `while`, never an `if`.** Spurious wakeups are
  normal in this design and in Linux's.
- **A question this Part makes visible and the next one would hide:** `console_input.c`'s ring
  buffer exists *because* the reader was the one draining it. Once the ISR feeds the `ldisc`
  directly, ask whether the ring buffer and its `queue_lock` still have a purpose.

**Verify:** boot, type nothing for ten seconds, then type. `task_dump_all()`'s `switch_count` for
the shell must be **flat** across the idle period and climb again after the keystroke — that is
the whole Step, and it is invisible any other way. Then the full Phase 6 acceptance run again, on
both architectures.

---

## 🕳️ Traps worth knowing about in advance

- **The lost wakeup.** Decision 3 exists because of it. It is timing-dependent, so it will not
  appear in testing and will appear later as a shell that hangs once.
- **Blocking inside a syscall happens with interrupts enabled.** `x86_64_syscall_handler()` calls
  `arch_irq_enable()` around `syscall_dispatch()`. So `sleep()` is reached with interrupts on, from
  a path that will re-disable them at trap exit — and the flags `spinlock_lock_irqsave()` captured
  have to survive the switch and come back to the right task. Two tasks each holding a saved
  interrupt state across a switch is a new situation in this tree.
- **The trampoline's return address on x86_64.** A1's whole risk, restated: eight bytes wrong and
  control transfers to an address nobody chose. Named struct fields for both the return address and
  the alignment padding are most of the defence; there is still no assertion between the mistake and
  the jump.
- **The trampoline is entered by `ret`, not by `call`.** So `rsp` is 16-aligned where SysV expects
  8, and the padding field is what fixes it. The symptom is the first `movaps` the compiler decides
  to emit inside that function, which may be zero for months.
- **`task_state_names[]` is indexed by state.** Adding `TASK_BLOCKED` without adding its string
  makes every dump print garbage for blocked tasks — during the Step where blocked tasks are the
  entire subject.
- **`BOOT` runs on Limine's stack and has no guard.** `task_stack_intact()` returns `true` for it
  unconditionally. As the scheduler thread it is now the most-executed stack in the system, and it
  is the one stack an overflow will not catch. Worth knowing before trusting a dump.
- **`arch_halt()` masks interrupts.** Reaching for it as the idle instruction produces a machine
  that goes to sleep and never wakes — which looks exactly like a scheduler bug.
- **`swapgs` is paired per *path*, not per privilege transition — and that is already a bug.**
  `isr.S` never touches GS; only `syscall_entry.S` does. After a context switch the frame you leave
  through does not belong to the trap you entered through, so a task can reach ring 3 with the
  kernel GS base still installed, and its next `syscall` loads `%rsp` from near address zero.
  Unreachable today — one user task, so the far side of every switch is kernel-mode `BOOT` — and
  **`fork` in Step 2 is what makes it live.** Unifying the x86_64 tail in A1 fixes it as a side
  effect. AArch64 solved the identical problem for `SP_EL0` by reading the answer out of the frame;
  see the comment in `vectors.S`.
- **Two copies of the trap-restore tail.** B1 warns about it for AArch64; the same applies to
  x86_64, where there are literally two. The trampoline and the stub tails must be one
  implementation.
- **An all-blocked system idles forever instead of shutting down.** Correct, not a bug: the
  scheduler cannot tell a deadlock from a machine waiting on a keystroke (Decision 5).
- **`ls /test/scratch` and the VFS self-tests run before the scheduler exists.** `pros.tests` runs
  `test_all()` from `main()` *before* `arch_irq_enable()`. Nothing in this Step should need those
  to move — if something does, that is a signal the restructure went further than intended.

---

## 📁 Files touched

New:

- ⬜ `kernel/src/arch/aarch64/switch.S` + a `struct switch_frame` header — B1
- ⬜ `kernel/src/arch/x86_64/switch.S` + a `struct switch_frame` header — A1

Existing, to be modified:

- ⬜ [`kernel/include/proc/task.h`](../kernel/include/proc/task.h) — the field rename
  (Decision 1) with a validity comment on each frame, `TASK_BLOCKED`, the `switch_frame` pointer,
  the channel a task is sleeping on
- ⬜ [`kernel/src/proc/task.c`](../kernel/src/proc/task.c) — `task_state_names[]`,
  `task_dump_all()` covering the scheduler thread, a fabricated initial `switch_frame` per new
  task
- ⬜ [`kernel/src/proc/sched.c`](../kernel/src/proc/sched.c) + `proc/sched.h` — the scheduler loop,
  `sched()`, `sleep`/`wakeup`, `sched_only_current_is_alive()` removed
- ⬜ [`kernel/include/arch/arch.h`](../kernel/include/arch/arch.h) — `arch_switch_to()`, the
  initial-`switch_frame` builder, `arch_trap_return()`, `arch_irq_enabled()`, and an idle primitive
  that is not `arch_halt()`
- ⬜ [`kernel/src/arch/x86_64/arch.c`](../kernel/src/arch/x86_64/arch.c) +
  [`aarch64/arch.c`](../kernel/src/arch/aarch64/arch.c) — the initial `switch_frame`, the idle
  primitive
- ⬜ [`kernel/src/arch/aarch64/vectors.S`](../kernel/src/arch/aarch64/vectors.S) — the restore tail
  factored out so the trampoline can reuse it
- ⬜ [`kernel/src/arch/x86_64/isr.S`](../kernel/src/arch/x86_64/isr.S) — its tail exported as the
  single `x86_64_trap_return`
- ⬜ [`kernel/src/arch/x86_64/syscall_entry.S`](../kernel/src/arch/x86_64/syscall_entry.S) — its
  duplicate copy of that tail replaced by a `jmp` to the unified one, taking the `swapgs` bug with it
- ⬜ [`kernel/include/core/spinlock.h`](../kernel/include/core/spinlock.h) + `core/spinlock.c` —
  `spinlock_lock`/`spinlock_unlock` exposed, the `_irqsave` pair recast as their composition
- ⬜ [`kernel/src/core/test/test_task.c`](../kernel/src/core/test/test_task.c) — one `task->frame`
  use, renamed; worth a companion check that a fresh `switch_frame` also lies inside its own stack
- ⬜ [`kernel/src/drivers/console_dev.c`](../kernel/src/drivers/console_dev.c) — the spin replaced
  by `sleep()`, and a `struct spinlock` beside the `ldisc`
- ⬜ [`kernel/src/core/console_input.c`](../kernel/src/core/console_input.c) — feeding the `ldisc`
  moves here, and `wakeup()` fires on a completed line
- ⬜ [`kernel/src/core/main.c`](../kernel/src/core/main.c) — falls into `scheduler()` forever

---

## 🪜 Verification

- **C0** compiles and proves nothing.
- **B1 and A1** are provable in isolation, before any scheduler change, with two throwaway kernel
  tasks switched by hand. **Take that opportunity** — it is the only point in the Step where the
  register save/restore can be tested without the scheduler restructure confusing the evidence.
- **C1's test is that nothing changed.** Phase 6's acceptance run, byte for byte, on both
  architectures.
- **C2's test is an absence**, and `switch_count` is the only instrument that shows it. Flat while
  idle, climbing after a keystroke.

When C2 passes on both architectures, **Step 1 is done**, and Phase 7's Decision 3 has lost its
first half — `read()` no longer spins. The second half is arbitration, not per-open state (see
Decision 3 as amended), and stays open until Step 2 creates something that can contend for it.
