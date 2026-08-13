# Working Document: Phase 3 Step 3 — Two Kernel Threads, Round-Robin [STATUS: NOT STARTED ⬜]

> [!NOTE]
> **Phase 3 Step 3**, from [`PHASE3_PREEMPTION.md`](PHASE3_PREEMPTION.md) Chapter 6, expanded
> into individually verifiable Parts. It draws on Chapter 1 (the trap frame *is* the process)
> and Chapter 4 (the scheduler). Read those for the *why*; this document is the *how*, in order.
>
> This is **Phase 3's payoff**. Everything before it was plumbing.

> [!NOTE]
> Mentor-mode reminder (see the note at the top of [`../README.md`](../README.md)): this is a
> design reference to code from by hand, not code to paste in. Signatures, struct fields and
> constants are given exactly, because guessing those wastes time without teaching anything —
> but the bodies are yours. Where a register value or a hardware behaviour is stated, confirm
> it against the manual; Step 1 and Step 2 both have corrections that exist because a draft was
> trusted.

---

## 🎯 What "done" looks like

The opposite of Step 2, and much more fun:

> On both architectures, three tasks — the boot task plus two kernel threads — take turns on
> the CPU, driven only by the timer interrupt. Their output **interleaves**. Nobody yields;
> nobody cooperates. Each task's switch counter reads roughly 100 after three seconds at
> 100 Hz, and the machine still shuts down cleanly on its own.

Interleaved output from code that never yields is the whole milestone. It happens **entirely in
ring 0**, where a mistake prints a register dump instead of resetting the machine.

**What is explicitly *not* in this step:**

- ❌ No ring 3 / EL0. Both threads are kernel threads at the kernel's privilege level.
- ❌ No address-space switching. All three tasks share the kernel context; `task->ctx` stays
  unused. `vmm_switch_context()` is not called. That's Phase 5.
- ❌ No blocking, sleeping, or wait queues. Tasks busy-loop. `switch_to` and voluntary
  switching are Phase 7 — the frame-swap deliberately doesn't cover them.
- ❌ No task exit or reaping. A kernel thread here runs forever; exiting is Phase 7.
- ❌ No priorities, no fairness beyond round-robin, no SMP.
- ❌ No FPU/SIMD save-restore. Still no *user* tasks, so nothing to corrupt.

---

## 🧠 The mental model, before any code

Step 2 built a machine that can restore *any* frame the handler names. It has only ever been
handed back the frame it was given. Step 3 hands it a different one.

That's it. That is the entire context switch. There is no register-saving code to write,
because the trap stub already saved everything on entry.

### The two halves, and why only one is new

| Half | Where it comes from | Status |
|---|---|---|
| **Suspending a task** | the trap stub built its frame on its own kernel stack, and `sched_on_trap_exit()` already stores that pointer in `current->frame` | ✅ built and running |
| **Resuming a task** | return *that task's* stored frame instead of the current one | ⬜ this step |

Suspending is already correct today and has been on every trap since Step 2. The only reason
nothing happens is that `sched_pick_next()` returns `current`.

### The bootstrap asymmetry, which is the one genuinely new idea

A task that has run before has a real frame: the trap stub wrote it, and it describes a real
interrupted moment. A **brand-new** task has never trapped, so nothing has ever written a frame
for it — but the only way to start it is to *return from a trap into it*.

So you forge one. You write, by hand, the frame that the trap stub *would* have written if this
task had been interrupted at the first instruction of its entry function. Then you hand it to
the stub, which cannot tell the difference and doesn't try.

```
   ┌──────── task 1's kernel stack (fresh from kstack_alloc) ────────┐
   │ [guard]                                    [fabricated frame]  │
   └────────────────────────────────────────────────────────────────┘
        base                                          ▲        stack top
                                                      │
                              task->frame ────────────┘
                                                      │
              sched_on_trap_exit() returns it ────────┘
                                                      │
                          the stub restores it, and the task "resumes"
                              at an instruction it has never executed
```

**This is why frame fabrication is architecture-specific.** The frame is a hardware contract:
x86_64's `iretq` consumes `rip`/`cs`/`rflags`/`rsp`/`ss`, AArch64's `eret` consumes `elr` and
`spsr`. Generic code cannot fill those in, which is why this step adds exactly one new
`arch_*` hook.

### Where a switch is allowed to happen

Unchanged from Step 2, and worth restating because it's what keeps this safe:

**`need_resched` is set in the timer path; it is acted on only in `sched_on_trap_exit()`.** The
decision to switch and the act of switching happen in different places, and the act happens at
exactly one point in the kernel — on the way out of a trap, when the frame is complete and no
kernel data structure is half-updated. The kernel is **not preemptible**; it is *preempted at
trap exit*, which is a different and much easier thing.

---

## ⚖️ Three decisions to make before writing anything

### 1. What shape is the run queue?

`struct task` already has a `next` pointer. **Recommendation: a circular singly-linked list**,
with `sched_pick_next()` returning `current->next`. Round-robin becomes one line, and the
one-task case degenerates correctly: `task0->next == task0`, so the identity behaviour of
Step 2 survives untouched.

The alternative — an array with an index — needs a count, a modulo, and a compaction story the
moment a task exits. The list needs none of that, and Phase 7's `fork` will want O(1) insert
anyway.

### 2. How long is a time slice?

**Recommendation: one tick.** Switch on every timer interrupt. At 100 Hz that's every 10 ms,
which is aggressive for a real scheduler and perfect for a demo — maximum switching means
maximum chance of catching a bug, and the interleaving is unmistakable.

Define the constant anyway (`SCHED_TIME_SLICE_TICKS`) so the knob exists and is named, even
while it's 1. Turning it up is the first thing you'll want when the output is too noisy.

### 3. Do the threads print directly, or set a flag for the boot task to print?

**Recommendation: print directly, and accept mid-line interleaving.** `kprintf` is not
reentrant and nothing locks it, so a timer interrupt landing in the middle of one task's
`kprintf` will splice another task's output into the line.

That looks alarming and is **exactly what preemption looks like**. It is the most direct
evidence you will get that nothing here is cooperative. If it gets too noisy to read, raise
`SCHED_TIME_SLICE_TICKS` rather than adding a lock — a lock is a Phase 7 concept and adding one
now would hide the very thing this step exists to show.

---

## 🗺️ Suggested order

Parts are labelled by track — **C** architecture-neutral, **A** x86_64, **B** AArch64.

```
  C0 ── C1 ──┬── A1 ──┬── C2 ── C3
             └── B1 ──┘
   │    │         │      │     │
   │    │         │      │     └─ make the invariants real
   │    │         │      └─ THE PAYOFF: add task 1 to the run queue
   │    │         └─ fabricate a frame (per arch), inspect it without running it
   │    └─ build a task without scheduling it
   └─ run queue that advances, with one task in it
```

The shape is deliberate: **every Part before C2 is verifiable while still running exactly one
task.** You build the run queue, then a task, then its frame, and inspect each — and the boot
log stays boring until the moment you choose to make it interesting. If something is wrong,
you find out with one task on the CPU, not three.

**C0 contains a latent panic that Step 2 left behind.** Read it before anything else.

---

## 🧩 C0 — A run queue that advances ⬜

**Goal:** `sched_pick_next()` walks a circular list. With one task in it, nothing changes.

### The bug Step 2 left, which C0 must fix first

`sched_on_trap_exit()` currently does this:

```c
if (!kstack_guard_intact(next->kernel_stack_top)) kpanic("kernel stack overflow");
```

That is unreachable today, because it only runs when `next != current` and `next` is always
`current`. The moment there are two tasks, **it panics on the first switch back to task 0.**

Trace it: `kstack_guard_intact()` reads `stack_top - KSTACK_SIZE`. Task 0's `kernel_stack_top`
is a point *inside* Limine's boot stack (`task_init_boot()` records the live `sp`), so that
subtraction lands 16 KiB below it — a mapped HHDM address holding unrelated data, which will
not equal `KSTACK_GUARD`. The guard reports a stack overflow that didn't happen, and the kernel
dies on the first round trip.

`kstack_guard_intact()` isn't wrong; it's being asked a question it can't answer. Task 0 has
**no guard**, because it has no known stack base — which `task_dump()` already knows and
handles. The check needs to be asked of the *task*, not the stack:

```c
// proc/task.h
// True when the task's stack guard is intact, or when the task has no guard to check.
// Task 0 runs on Limine's boot stack: no base, no extent, nothing to place a guard in.
bool task_stack_intact(const struct task *task);
```

**Check `current` as well as `next`.** The task that just ran is the one that could have
overflowed, so checking it at the moment it's switched out attributes the failure correctly.
Checking only the incoming task reports the overflow one switch late, blaming the wrong task.

### The run queue

Add to `struct task` — the `next` field already exists and becomes the queue link:

```c
// proc/task.h
struct task {
    uint64_t pid;
    int state;
    const char *name;          // NEW: for the dump; a switch counter with no name is a riddle
    void *kernel_stack;        // base, for freeing and for the guard check
    void *kernel_stack_top;    // what arch_set_kernel_stack() gets
    struct trap_frame *frame;  // valid only while suspended
    struct vmm_context *ctx;   // unused in this step; all tasks share the kernel context
    uint64_t switch_count;     // NEW: how many times this task has been switched *to*
    struct task *next;         // run-queue link; the list is circular
};
```

`switch_count` is not decoration — it turns "the output looks interleaved" into a number you
can check against `ticks / SCHED_TIME_SLICE_TICKS / task_count`. It's the difference between
believing the scheduler is fair and knowing it.

New scheduler surface:

```c
// proc/sched.h
#define SCHED_TIME_SLICE_TICKS 1   // switch on every tick; see decision 2

void sched_init(void);

// Insert a task into the run queue. Not safe against a concurrent switch — call it
// with IRQs disabled, or before arch_irq_enable(), which is all this step needs.
void sched_add_task(struct task *task);

struct task *sched_get_current_task(void);
struct trap_frame *sched_on_trap_exit(struct trap_frame *frame);
void sched_on_timer_tick(void);
```

In `sched.c`, keep a stable `static struct task *run_queue_head` (task 0) and insert after it,
rather than inserting after `current` — `current` moves, and a list that reshapes itself
depending on who happens to be running is a debugging experience nobody needs.

`sched_init()` sets `task0->next = task0`, making a one-element cycle. `sched_pick_next()`
becomes `return current->next;` — which returns `current` when the cycle has one element, so
Step 2's behaviour is preserved exactly.

**Verify:** boots green on both arches, identical to Step 2's log. The one-element cycle means
`sched_pick_next()` returns `current`, `sched_on_trap_exit()` takes its early return, and
nothing switches. Confirm the guard fix by temporarily forcing the check to run on task 0 —
if it panics, `task_stack_intact()` isn't being used.

---

## 🧩 C1 — Build a task without scheduling it ⬜

**Goal:** a second `struct task` exists, fully formed, with its own kernel stack — and never
runs.

Creating a task and *running* it are separate concerns, and separating them buys a verification
point: you can inspect everything about a new task while the machine is still single-tasked and
completely safe.

```c
// proc/task.h

// Create a kernel thread. Allocates a struct task and a kernel stack, assigns a pid, and
// fabricates the initial trap frame via arch_task_init_frame().
// The task is NOT added to the run queue — that is sched_add_task()'s job.
// `entry` must never return; see task_exit_guard().
// Returns NULL if either allocation fails.
struct task *task_create(const char *name, void (*entry)(void));

// Free a task's kernel stack and the struct itself. Must not be the running task,
// and must not be in the run queue.
void task_destroy(struct task *task);

// True when `frame` lies inside this task's kernel stack. Returns true for a task with no
// known extent (task 0), because "unknown" is not the same as "wrong".
bool task_owns_frame(const struct task *task, const struct trap_frame *frame);

// Where a kernel thread lands if its entry function ever returns. Panics, loudly.
void task_exit_guard(void);

void task_dump(struct task *task);
void task_dump_all(void);   // walks the run queue; worth calling from the panic path
```

### The thing that has no return address

A kernel thread is entered by returning from a trap, not by a `call`. **Nothing pushed a return
address**, so if the entry function ever executes `ret`, it pops whatever garbage is on the
stack and jumps there. The symptom is a wild branch with no stack trace and no clue.

`task_exit_guard()` is the fix, and it costs nothing: arrange for it to be where a stray return
lands, and have it `kpanic("kernel thread returned")`. A1 and B1 each wire it up in the way
their architecture demands — and they are genuinely different, which is instructive.

For this step, entry functions loop forever and the guard should never fire. Write it anyway.
The one time it fires, it will save an afternoon.

### The stack, and one number that matters

`kstack_alloc()` already does the work: 4 pages, page-aligned, guard value written at the base.
`task_create()` calls it, records both `kernel_stack` (base, for freeing and the guard check)
and `kernel_stack_top`, then hands the top to `arch_task_init_frame()`.

`pid` comes from a `static uint64_t next_pid = 1;` — task 0 is the boot task and takes 0 in
`task_init_boot()`.

**Verify:** call `task_create()` in `main.c` before `arch_irq_enable()`, then `task_dump_all()`.
Two lines, the new task `READY` with a real `kstack` range and `guard ok`. Boot is otherwise
byte-for-byte Step 2's, because nothing put the task in the run queue. Also confirm the free
path: `task_destroy()` the task immediately, and check `pmm_get_free_page_count()` returns to
where it started — the pattern `test_kstack.c` already uses.

---

## 🅰️ A1 — x86_64: fabricate a frame ⬜

**Goal:** write the frame `iretq` needs in order to "return" into a function that has never run.

The one new architecture hook in this step:

```c
// include/arch/arch.h

// Fabricate the initial trap frame for a brand-new kernel thread: exactly what the trap stub
// would have written had this task been interrupted at the first instruction of `entry`.
// The frame is placed at the top of the task's kernel stack. Returns a pointer to it, for
// storing in task->frame.
struct trap_frame *arch_task_init_frame(void *kernel_stack_top, void (*entry)(void));
```

Implement it in `arch/x86_64/idt.c` — that file already owns `struct trap_frame`'s layout and
the trap path, and keeping frame knowledge in one place per architecture is worth more than
tidiness about which file is "the arch API".

Constants (`idt.h`, next to the GDT setup that produces them):

```c
#define X86_64_SELECTOR_KERNEL_CODE 0x08         // gdt.kernel_code, from tss_init()
#define X86_64_SELECTOR_KERNEL_DATA 0x10         // gdt.kernel_data
#define X86_64_RFLAGS_RESERVED_BIT1 (1ULL << 1)  // architecturally always 1
#define X86_64_RFLAGS_IF            (1ULL << 9)  // interrupts enabled
```

`rflags` must be `X86_64_RFLAGS_RESERVED_BIT1 | X86_64_RFLAGS_IF` — `0x202`. **Bit 1 is not
optional**; it reads as 1 always, and a frame with it clear is a frame that didn't come from
real hardware.

**If `IF` is clear, the task runs exactly once and the machine wedges.** It gets the CPU, takes
no timer interrupt because interrupts are masked, and never gives it back. That failure looks
exactly like a scheduler bug and isn't one.

### The trap that will cost you a day if you don't know it

> [!IMPORTANT]
> **In 64-bit mode, `iretq` pops `SS:RSP` unconditionally** — including on a same-privilege
> return. This differs from 32-bit protected mode, where `SS:ESP` is popped only on a privilege
> change, and it is the single most surprising thing about this Part.
>
> So `frame->rsp` and `frame->ss` are **live fields for a ring-0 kernel thread**, not
> placeholders that only matter once userland exists. Leave `rsp` zero and the task starts
> executing with a null stack pointer; the first `push` faults, and the reported address points
> at perfectly good code.
>
> Confirm it in the SDM's IRET pseudocode rather than taking it from this file.

### Laying out the top of the stack

Two constraints pull in different directions, and satisfying both is worth doing deliberately:

- **A stray `ret` must land somewhere legible** → put `task_exit_guard`'s address at the very
  top of the stack and point `frame->rsp` at it.
- **The entry function wants SysV alignment** → the ABI has `rsp` 16-byte aligned *before* a
  `call`, so at function entry `rsp ≡ 8 (mod 16)`. Enter with `rsp ≡ 0` and any `movaps` the
  compiler emits against a 16-aligned stack slot faults. The x86_64 kernel still builds with
  SSE enabled, so this is reachable.

Both fall out of one layout:

```
kernel_stack_top ──────────────────────────────┐
                       [top-8] = task_exit_guard│  <- a stray `ret` lands here
                       ...frame, 16-aligned...  │
```

- `guard_slot = (uint64_t *)((uint8_t *)kernel_stack_top - 8)`, holding `(uint64_t)task_exit_guard`
- `frame` = `guard_slot - sizeof(struct trap_frame)`, **rounded down to a 16-byte boundary**
- `frame->rsp = (uint64_t)guard_slot` — which is `top - 8`, so `rsp ≡ 8 (mod 16)` ✓

Rounding the frame down rather than inserting a magic padding constant keeps the intent visible.

Fields to fill: `rip` = `entry`, `cs` = kernel code, `ss` = kernel data, `rflags` = `0x202`,
`rsp` = the guard slot. Zero the general-purpose registers — a fresh task has no register state
and zeros are far easier to recognise in a dump than stack residue. Set `int_no` to something
identifiable (`0xFF` reads as "fabricated" in a dump); it and `error_code` are discarded by the
stub's `addq $16, %rsp`, so their values only ever matter to you.

**Verify without running it.** Print the fabricated frame's `rip`, `cs`, `rflags`, `rsp`, `ss`
and its own address, and check them against a *real* frame — C4's technique, and the reliable
way to get these constants right. Add a one-shot print of the live frame in the timer branch
and compare: `cs` and `ss` should match exactly, and `rflags` should match in bits 1 and 9.
**If the real frame's `rflags` disagrees with your fabricated `0x202` in any bit you care
about, believe the hardware.** Delete the print afterwards.

---

## 🅱️ B1 — AArch64: fabricate a frame ⬜

**Goal:** the same thing for `eret`, which is a shorter list and one sharp edge.

Implement in `arch/aarch64/exceptions.c`, for the same reason as A1.

Constants (`exceptions.h`):

```c
#define AARCH64_SPSR_M_EL1H 0x5ULL       // M[3:0] = 0b0101: return to EL1, using SP_EL1
#define AARCH64_SPSR_D      (1ULL << 9)  // debug masked
#define AARCH64_SPSR_A      (1ULL << 8)  // SError masked
#define AARCH64_SPSR_I      (1ULL << 7)  // IRQ masked  <- must be CLEAR to be preemptible
#define AARCH64_SPSR_F      (1ULL << 6)  // FIQ masked

#define AARCH64_VECTOR_CURRENT_EL_IRQ 5  // vector slot 5: current EL on SP_EL1
```

`spsr` = `AARCH64_SPSR_M_EL1H` — `0x5`, with every DAIF bit clear. **`I` clear is what makes
the thread preemptible**, and it's the exact analogue of x86's `IF`: set it and the task runs
once and never yields. Masking `F` as well (`0x45`) is defensible since nothing routes FIQ; do
it deliberately or not at all.

### The field the exit path actually reads

> [!IMPORTANT]
> **`frame->vector_type` must be a current-EL slot (4-7).** This is a direct consequence of
> Step 2 Part B2: the exit path in `vectors.S` reads `vector_type` back out of the frame and
> compares it against 8, and on `>= 8` it writes the frame's `sp` field into `SP_EL0`.
>
> Leave `vector_type` at zero and you get away with it. Leave it as uninitialised stack residue
> that happens to be ≥ 8, and the first resume writes a garbage `sp` into userland's stack
> pointer register. Nothing breaks *now*, which is precisely what makes it expensive later.
>
> Set it to `AARCH64_VECTOR_CURRENT_EL_IRQ`. This is the frame describing itself as "I came
> from a kernel IRQ", which is exactly what it's pretending to be.

That field is worth pausing on. It's the payoff of B2's discovery — **which stack pointer a
frame returns to is a property of the frame, not of the trap that arrived** — and a fabricated
frame is the first case where nothing else could possibly have supplied the answer.

### Layout

Simpler than x86, because AArch64 has a link register:

- `frame` = `kernel_stack_top - TRAP_FRAME_SIZE`
- `frame->x[30]` = `(uint64_t)task_exit_guard` — a stray `ret` is `br x30`, so the guard is just
  the initial LR. No stack slot needed.
- `frame->elr` = `entry`
- `frame->spsr` = `0x5`
- `frame->vector_type` = 5
- `frame->sp` = `kernel_stack_top` — ignored by the exit path for a current-EL frame, but
  `task_dump()` and the panic dump both print it, and a field that lies is worse than a field
  that's empty
- `frame->esr`, `frame->far`, `x[0..29]` = 0

> [!WARNING]
> Use **`TRAP_FRAME_SIZE` (304)**, not `sizeof(struct trap_frame)` (296). The exit path ends
> with `ldp x0, x1, [sp], #TRAP_FRAME_SIZE`, so the stack pointer the task starts with is
> `frame + 304`. Place the frame using `sizeof` and the task begins life with `sp` 8 bytes past
> the top of its own stack, corrupting whatever sits above it. The `_Static_assert`s in
> `exceptions.h` pin the two together — they cannot catch you using the wrong one.

304 is a multiple of 16 and `kernel_stack_top` is page-aligned, so the frame and the resulting
`sp` are both 16-byte aligned. That is not luck, and it is load-bearing: AArch64 faults on a
misaligned `sp` used as a base address.

**Verify:** the same comparison as A1. Print the fabricated frame next to a real one from the
timer branch; `spsr` and `vector_type` should match a live kernel IRQ frame exactly. If your
fabricated `spsr` differs from the live one in the DAIF bits, that difference is your answer
about whether the task will be preemptible.

---

## 🧩 C2 — Two threads, and the payoff ⬜

**Goal:** interleaved output.

Everything is built. This Part adds `sched_add_task()` calls and two functions to `main.c`.

### The threads

```c
// core/main.c
static void kthread_a(void) { ... }
static void kthread_b(void) { ... }
```

Each should:

- **loop forever.** Not because exiting is hard, but because it doesn't exist yet. The
  `task_exit_guard()` is there for the day you forget.
- **print on a tick boundary**, not every iteration. Watch `timer_get_ticks()` and print when
  it crosses a multiple of ~25 — four lines a second per thread, which is legible. Printing
  every iteration produces thousands of lines a second and proves nothing extra.
- **do nothing else.** No shared state between the threads. Not because sharing is wrong, but
  because the first time two tasks race over a variable you want to be *looking for it*, not
  discovering it inside the milestone that was supposed to be the reward.

Wire it up in `main.c`, **before `arch_irq_enable()`** — `sched_add_task()` walks the list the
timer path also reads, and doing it with interrupts off is free here and correct:

```c
sched_init();
struct task *a = task_create("kthread-a", kthread_a);
struct task *b = task_create("kthread-b", kthread_b);
if (!a || !b) kpanic("task_create failed");
sched_add_task(a);
sched_add_task(b);
task_dump_all();
...
arch_irq_enable();
```

Task 0 stays in the queue and keeps running `_start`'s countdown, so the three-second shutdown
still works — now sharing the CPU with two threads that don't know it exists.

### `sched_on_trap_exit()`, final shape

The Step 2 body plus four lines. In order:

1. `current->frame = frame` — unchanged, and already correct.
2. Early-return unless `need_resched`; clear it.
3. `next = sched_pick_next()`; early-return if `next == current`.
4. **`task_stack_intact()` on `current` first, then `next`** — see C0.
5. `current->state = TASK_READY`, `next->state = TASK_RUNNING`, `next->switch_count++`.
6. `arch_set_kernel_stack(next->kernel_stack_top)`, `current = next`.
7. `return next->frame;`

Step 6 is worth understanding rather than copying. On x86_64 it writes `tss.rsp0`, which the
CPU will consult on the next trap *from a lower privilege level* — so it does nothing
observable in this step and is exactly right anyway. On AArch64 the body is empty, because
`SP_EL1` already holds the right value by the time `eret` runs. Trace both once; the asymmetry
is Chapter 2's whole point and this is where it stops being theoretical.

**Verify — this is the milestone.**

```
[TASK ] pid 0 (boot)      RUNNING  boot stack, sp was 0x...,  extent unknown, guard n/a   switches 0
[TASK ] pid 1 (kthread-a) READY    kstack 0x...-0x...  guard ok   switches 0
[TASK ] pid 2 (kthread-b) READY    kstack 0x...-0x...  guard ok   switches 0
[IRQ  ] Enable IRQ
[K    ] Count to 3:
[T-A  ] tick 25
[T-B  ] tick 25
[T-A  ] tick 50
[T-B  ] tick 50
[TIMER] 1
...
[K    ] All done here, shutting down.
```

Three things to check, in order of how much they prove:

1. **Both threads print.** Preemption happened at least twice.
2. **They keep printing, alternating, for the full three seconds**, and the countdown still
   reaches 3. All three tasks are getting the CPU; nobody is starved.
3. **`task_dump_all()` at the end shows switch counts near `300/3 = 100` each.** This is the
   real proof. Interleaved output is consistent with a scheduler that switches erratically;
   three near-equal counters are not.

Expect occasional garbled lines where one task's output splices into another's. That is
`kprintf` being non-reentrant, it is not a bug, and it is the most direct evidence you will get
that nothing here is cooperative.

---

## 🧩 C3 — Make the invariants real ⬜

**Goal:** the assertions written in Step 2 "for later" now have something to catch.

Cheap, and this is the moment they stop being theoretical:

- **`task_owns_frame()` on entry to `sched_on_trap_exit()`.** Step 2 Part C3 called for this and
  noted it could not fire with one task on an unbounded boot stack. With two tasks on real
  stacks it becomes a genuine invariant: the frame arriving must lie inside the running task's
  kernel stack. If it doesn't, either the switch handed back the wrong frame or a stack
  overflowed into a neighbour. Keep returning `true` for task 0 — "unknown extent" is not
  "wrong".
- **`task_dump_all()` from the panic path.** `panic_unhandled()` already dumps registers; the
  question it can't answer is *which task was running*. One call closes that gap permanently,
  and every remaining bad day in Phase 3 begins with that question.
- **A high-water mark.** Scan the task's stack from the base upward for the first non-zero word
  and report used-vs-total in `task_dump()`. `kstack_alloc()` doesn't zero the stack today, so
  either zero it there or skip the mark and say so — an approximate number nobody can trust is
  worse than no number. 16 KiB is not much once a nested trap and a recursive page-table walk
  are both on it.

**Verify:** boot green with everything on. Then break it deliberately once — hand
`sched_on_trap_exit()` the *other* task's frame and confirm `task_owns_frame()` catches it with
a legible message rather than a silent wild jump. Undo it immediately; like C4, it's a
demonstration, not a test.

---

## 🔬 Debugging tools worth knowing before you need them

Step 1's list (`-d int`, `-d int,cpu_reset`, `-D logs/…`, the QEMU monitor) and Step 2's
(`info registers`, `nm | sort`) both still apply. Two more matter specifically here:

- **`switch_count` is a debugger.** A task stuck at zero was never scheduled — a run-queue
  problem. A task that climbs while others don't is a `pick_next()` problem. A task that
  climbs then stops is a frame problem: it ran, and what it resumed into wasn't what you
  fabricated. Three different bugs, distinguished by one counter and no tooling.
- **Print the address of `frame` at trap exit alongside `current->pid`.** Step 2's most useful
  habit, now with a task attached. Consecutive traps from one task show addresses drifting
  within one stack; a switch shows the address jumping to a different stack entirely. That
  jump *is* the context switch, visible.

---

## 🕳️ Failure modes, collected

Grouped by symptom, because that's how you'll meet them.

**One thread prints, then the machine wedges. No panic, no reset.**
- The fabricated frame has interrupts masked: `IF` clear in `rflags` (A1) or `I` set in `spsr`
  (B1). The task got the CPU, took no timer interrupt, and never gave it back. The most common
  failure in this step, and it looks like a scheduler bug from the outside.

**Immediate fault the first time the second task runs, address near zero.**
- A1: `frame->rsp` left zero. `iretq` popped it, and the task's first `push` faulted. Remember
  that 64-bit `iretq` pops `SS:RSP` even without a privilege change.

**AArch64 corrupts memory just above a task's stack.**
- B1: the frame was placed using `sizeof(struct trap_frame)` (296) instead of `TRAP_FRAME_SIZE`
  (304), so the task starts with `sp` 8 bytes past its own stack top.

**AArch64 boots, switches, and something is subtly wrong later.**
- B1: `frame->vector_type` ≥ 8, so the exit path wrote the frame's `sp` into `SP_EL0`. Harmless
  until Phase 4, then not.

**Panic: "kernel stack overflow" on the very first switch.**
- C0's latent bug: `kstack_guard_intact()` called on task 0, whose `kernel_stack_top` points
  into Limine's stack. Use `task_stack_intact()`.

**Panic: "kernel thread returned".**
- Working exactly as intended. An entry function fell off its end. Add the loop.

**#GP on x86 with a fault address inside good code.**
- A1: misaligned frame, discovered by `iretq` rather than by the instruction that caused it —
  the same signature as Step 2's C1 warning. Round the frame down to 16.

**Everything interleaves, but one task's counter is far behind.**
- `sched_add_task()` inserted relative to `current` rather than a stable head, and reshaped the
  list depending on who was running when it was called. Or `need_resched` is being cleared
  somewhere it shouldn't be.

**Output is garbled mid-line.**
- Not a failure. That's `kprintf` being non-reentrant across a preemption, and it's evidence.
  Raise `SCHED_TIME_SLICE_TICKS` if it's unreadable.

---

## 📁 Files touched

- ⬜ `kernel/include/arch/arch.h` — `arch_task_init_frame()`
- ⬜ `kernel/arch/x86_64/idt.h` — selector and `RFLAGS` constants
- ⬜ `kernel/arch/x86_64/idt.c` — `arch_task_init_frame()`
- ⬜ `kernel/arch/aarch64/exceptions.h` — `SPSR` and vector-slot constants
- ⬜ `kernel/arch/aarch64/exceptions.c` — `arch_task_init_frame()`
- ⬜ `kernel/include/proc/task.h` — `name`, `switch_count`; `task_create()`,
  `task_destroy()`, `task_stack_intact()`, `task_owns_frame()`, `task_exit_guard()`,
  `task_dump_all()`
- ⬜ `kernel/proc/task.c` — the above, plus `pid` allocation
- ⬜ `kernel/include/proc/sched.h` — `SCHED_TIME_SLICE_TICKS`, `sched_add_task()`
- ⬜ `kernel/proc/sched.c` — circular run queue, real `sched_pick_next()`, the switch body
- ⬜ `kernel/core/main.c` — two thread functions, `task_create()` + `sched_add_task()` before
  `arch_irq_enable()`, `task_dump_all()` before and after
- ⬜ `kernel/arch/aarch64/exceptions.c` / `kernel/arch/x86_64/idt.c` — `task_dump_all()` from
  the panic path (C3)

Deliberately **not** touched: `vectors.S` and `isr.S` — if a change wants to reach into the trap
stubs, Step 2 got something wrong and that is the bug to fix. Also untouched: `mm/`, `fs/`, the
GDT, `syscalls.h`.

---

## 🪜 Verification

`core/test/` reaches more of this step than it did of Step 2, because task creation is a pure
function of data even though scheduling isn't. Worth adding to `test_kstack.c`'s neighbourhood
as `core/test/test_task.c`:

- **`task_create()` then `task_destroy()` leaks nothing** — `pmm_get_free_page_count()` returns
  to where it started. Same pattern as the existing kstack test.
- **A fresh task's frame lies inside its own kernel stack** — `task_owns_frame(t, t->frame)`.
  This is the C3 invariant, testable at rest.
- **A fresh task's guard is intact** and `task_stack_intact()` agrees.
- **`task_stack_intact()` returns true for a task with no stack base** — the C0 bug, pinned so
  it cannot come back.

The rest is the boot log:

| Check | Expected |
|---|---|
| C0, both arches | identical to Step 2 — one-element cycle, no switching |
| C1, both arches | one extra task-dump line, still no switching |
| A1 / B1 fabricated vs live frame | `cs`/`ss` or `spsr`/`vector_type` match exactly |
| C2, both arches | **interleaved output from two threads**, countdown still reaches 3 |
| C2 switch counters | three near-equal counts, ≈ `ticks / SCHED_TIME_SLICE_TICKS / 3` |
| C3 deliberate wrong frame | `task_owns_frame()` panics legibly |

One thing worth carrying forward rather than doing here: **the moment a task can block, this
scheduler is wrong.** `sched_pick_next()` returns `current->next` unconditionally, with no
notion of a task being ineligible to run. That's correct while every task is always runnable
and becomes a bug the instant one isn't — which is Phase 7's `switch_to` and wait queues.
`TASK_READY` / `TASK_RUNNING` are the two states this step needs; resist adding the others
until something can actually be in them.
