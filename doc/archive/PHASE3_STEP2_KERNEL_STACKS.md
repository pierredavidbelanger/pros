# Working Document: Phase 3 Step 2 — `struct task`, Per-Task Kernel Stacks & the Trap-Stub Contract [STATUS: COMPLETE ✅]

> [!NOTE]
> **Phase 3 Step 2**, from [`PHASE3_PREEMPTION.md`](PHASE3_PREEMPTION.md) Chapter 10, expanded into
> individually verifiable Parts. It draws on Chapter 1 (the trap frame *is* the process),
> Chapter 2 (kernel stacks) and Chapter 5 (the surgical context switch). Read those for the
> *why*; this document is the *how*, in order.
>
> Nothing here involves a second task, a scheduler that actually schedules, userland, or
> privilege levels. The whole step is plumbing — deliberately.

> [!NOTE]
> Mentor-mode reminder (see the note at the top of [`../README.md`](../../README.md)): this is a
> design reference to code from by hand, not code to paste in. The snippets below are shapes —
> the interesting lines are usually the ones left out. Where a value or a register behaviour is
> stated, it's worth confirming against the manual rather than trusting this file. Step 1's
> document has a whole paragraph of corrections that exist because a draft was trusted.

---

## ✅ What actually got built

All eight Parts are done. Both architectures boot green, all self-tests pass, and the boot log
is what it was before Step 2 plus one task-dump line — which is the acceptance criterion.

Four places where the build diverged from the design above. They're recorded rather than
edited away, because the divergences are the interesting part.

**C0 — the offsets are shared, not asserted.** The design called for `_Static_assert`s pinning
`struct trap_frame`'s layout against the byte offsets `vectors.S` hard-codes. What got built is
a shared `arch/aarch64/trap_frame.h` defining `TRAP_FRAME_SIZE` / `TRAP_FRAME_OFF_*` once, which
both the C header and `vectors.S` include. Strictly better: an assert *detects* divergence,
a single definition makes it unrepresentable. The `304` vs `sizeof == 296` alignment gap the
design warned about is now expressed rather than commented.

**B2 — the exit path needs the vector group too, and can't use `.if` to get it.** The design
covers capturing the interrupted `sp` per entry group with `.if \num >= 8`, and stops there.
The return path has the same problem and cannot solve it the same way: `vector_common_stub` is
one shared block, not generated per slot, so there is no assembly-time constant to branch on.
The vector type is read back out of the frame and compared against 8 at run time.

That is not a workaround, it's the correct model, and it's worth stating plainly because
Step 3 depends on it: **which stack pointer a frame returns to is a property of the frame, not
of the trap that arrived.** After a context switch those are two different tasks, potentially
at two different exception levels. An entry-time `.if` would have been reading the wrong task's
answer the moment a real switch happened.

**C2 — kernel stacks got their own translation unit.** `kstack.c`/`kstack.h` rather than living
inside `task.c`, which made the guard-value and alloc/free self-tests trivial to write. Those
five tests are the only part of this step `core/test/` can reach, and they pass.

**C4 — the suggested probe address was wrong, and would have hung the machine.** See that Part
for the full correction; the short version is that reserved space inside the GIC distributor's
address window is not backed by a device model, so reading it raises an external abort rather
than a translation fault, and `try_handle_hhdm_mmio_fault()` "resolves" it forever.

### One honest gap, carried forward

`task_init_boot()` sets task 0's `kernel_stack_top` to the *current* `sp` — a point in the
middle of Limine's live boot stack — and `sched_init()` hands that to `arch_set_kernel_stack()`.
So on x86_64, `tss.rsp0` currently points into a stack that is actively in use.

Harmless today: nothing traps from ring 3, so the CPU never consults `rsp0`. It stops being
harmless at [`PHASE4_PRIVILEGE.md`](../PHASE4_PRIVILEGE.md) Step 1, where the first trap back from
userland would build its frame on top of task 0's live locals. C2 deliberately left task 0 on
Limine's stack — you can't move a stack you're standing on — so the fix belongs to whichever
Step first has a task that both owns a real kernel stack and returns from EL0. Recorded in
Phase 4's trap list so it isn't rediscovered by symptom.

---

## 🎯 What "done" looks like

The acceptance criterion is uncomfortable, because it's an absence:

> On both architectures, boot is byte-for-byte what it is today: self-tests pass, the timer
> ticks, three seconds are counted, `arch_shutdown()` runs, QEMU exits 0.

Nothing new is printed and nothing new happens. That *is* the point — every change in this step
replaces a mechanism with a more general one that behaves identically while there is only one
task. If something visible changes, something is wrong.

Because "nothing changed" is a terrible thing to debug against, four things below are added
purely so the step has something to *observe*. They are worth the effort:

1. **The trap frame address stops being a constant.** Today, on AArch64, every trap builds its
   frame at exactly `exception_stack_top - 304`. After this step it's built wherever the
   interrupted code's stack pointer was, so consecutive traps report *different* addresses.
   One `kprintf` of `regs` proves the global stack is really gone.
2. **`regs->sp` in the AArch64 panic dump starts telling the truth.** It is junk today (see B1).
3. **A nested trap survives.** A page fault taken *inside* the timer IRQ handler currently
   corrupts the outer frame on AArch64. After this step it nests properly.
4. **A boot-time task dump** — pid, state, kernel stack range, high-water mark. Cheap now,
   invaluable for every remaining step of Phase 3.

**What is explicitly *not* in this step:**

- ❌ No second task, no run queue, no round-robin. `pick_next()` returns the only task there is.
- ❌ No ring 3 / EL0, no user segments in the GDT, no `iretq`/`eret` into userland.
- ❌ No syscall entry path, no `svc` decode.
- ❌ No `switch_to` in assembly. The whole argument of Chapter 5 is that the frame-swap makes it
  unnecessary until Phase 7.
- ❌ No FPU/SIMD save/restore. Nothing to corrupt with one task.
- ❌ No fd tables moving into `struct task` — that's [`PHASE5_LOADING.md`](../PHASE5_LOADING.md) Step 2, and moving them now means writing
  `fork`'s hardest field before `fork` exists.

---

## 🧠 The mental model, before any code

Three invariants hold in the kernel today. This step breaks all three, one at a time.

| Invariant today | After Step 2 |
|---|---|
| A trap returns to exactly where it came from. | A trap returns to **whatever frame the handler names**. |
| Trap frames are built on one fixed stack per architecture. | Trap frames are built on **the stack of whoever trapped**. |
| The kernel's stack pointer is wherever `_start` left it, forever. | The kernel's stack pointer is a **property of a task**, and the CPU must be *told* where it is. |

The first is the whole context switch. The second is what makes the first safe. The third is
the only one that differs meaningfully between the two architectures, and understanding *why*
it differs is most of Chapter 2's value.

### The one picture worth holding

```
   ┌─────────── task A's kernel stack ───────────┐   ┌────── task B's kernel stack ──────┐
   │                                    [frame A]│   │                          [frame B]│
   └─────────────────────────────────────────────┘   └───────────────────────────────────┘
                       ▲                                              ▲
              stub built it here                          handler said "restore this one"
                       │                                              │
                       └──────────► C handler ────────────────────────┘
                                  (returns a pointer)
```

There is no register-saving code in a context switch, because the trap stub already saved
everything. The switch is a *pointer assignment*. Everything else in this step exists to make
that pointer assignment safe.

### Why the two architectures need different help

This is the piece that repays actually understanding rather than memorising.

**x86_64 has one stack pointer register.** When `iretq` returns to ring 3, it pops the user's
`rsp` into the one and only `rsp`. The kernel stack pointer is *destroyed* by the return. So
when the next trap comes in, the CPU has nowhere to get a kernel stack from — which is why it
goes and reads `tss.rsp0`. **That is the entire purpose of the TSS in 64-bit mode.** It is not
a task-switching mechanism any more; it's a small table of stack pointers the CPU consults when
it has no other way to know.

**AArch64 has two.** `SP_EL0` and `SP_EL1` are separate registers; taking an exception to EL1
sets `PSTATE.SP = 1` and the live `sp` becomes SP_EL1 without disturbing SP_EL0. Nothing is
destroyed, so nothing needs looking up. The kernel stack pointer for the next trap is simply
"whatever SP_EL1 holds", and the return-from-trap path leaves it at the right value on its own.

The practical consequence: `arch_set_kernel_stack()` writes `tss.rsp0` on x86_64 and has an
**empty body** on AArch64 — with a comment explaining the invariant that makes the emptiness
correct, because an unexplained empty function reads as an unfinished one.

---

## ⚖️ Three decisions to make before writing anything

Each of these changes what the rest of the step looks like, so they're worth settling first.

### 1. Does the kernel move to EL1h? (AArch64)

**Recommendation: yes**, and it's B1 below. Today Limine leaves `SPSel = 0`, so kernel code
runs on SP_EL0 — the register that is supposed to belong to *userland*. That is fine while
userland doesn't exist and untenable the moment it does. It also makes "build the frame on the
stack the trap arrived on" impossible, because the stack the trap arrives on is SP_EL1, which
this kernel never sets.

The alternative — stay EL1t and seed SP_EL1 with a per-task kernel stack — technically works,
but leaves SP_EL0 meaning "kernel stack" for kernel threads and "user stack" for user tasks,
which is exactly the kind of two-meanings-one-register that produces unexplainable bugs in
[`PHASE4_PRIVILEGE.md`](../PHASE4_PRIVILEGE.md) Step 1.

### 2. Where do kernel stacks come from?

| Source | Pro | Con |
|---|---|---|
| `kmalloc(16384)` | one line, `HEAP_ALIGNMENT` is already 16 | no page alignment, no room for a guard *page* later |
| `pmm_alloc(4)` + `pmm_phys_to_virt()` | page-aligned, contiguous, trivially freed | bypasses the heap's bookkeeping |
| `vmm_map_page()` into a kernel range, with an unmapped page below | real guard page: overflow becomes a clean fault | needs a kernel virtual address allocator, which doesn't exist |

**Recommendation: `pmm_alloc(4)`.** Page alignment costs nothing and is what the third option
will need when it eventually happens. Pair it with a **guard value** (a magic `uint64_t` at the
lowest address of the stack, checked on every switch) — the poor man's version of the third
row, and it catches the failure that otherwise presents as random heap corruption three
subsystems away.

Sizing: 16 KiB / 4 pages, matching the existing exception stacks. It must hold the trap frame
plus the deepest kernel call chain, and `vmm_free_table_recursive()` (`mm/vmm.c:76`) recurses,
so that chain isn't trivially bounded.

### 3. Where does the task code live?

`kernel/proc/task.c` + `sched.c`, per Chapter 10's file list. Note the trap: **`kernel/Makefile:30`
globs `find core mm drivers fs`** — a new top-level directory is invisible to the build until
it's added to that list, and the symptom is a linker error naming a function you are looking
right at.

Putting it in `core/` instead avoids that entirely and is a defensible call for two files. The
argument for `proc/` is that `elf.c`, and later `signal.c`, `fork.c`, belong with it, and
`core/` is already the junk drawer.

---

## 🗺️ Suggested order

The Parts below are labelled by track — **C** architecture-neutral, **A** x86_64, **B**
AArch64 — so the letter says *where* the work lands and this diagram says *when*.

Unlike Step 1, this is mostly *shared* work with two arch-specific detours, so the shape is
different: a generic spine with a short x86 branch and a longer ARM branch.

```
  C0 ── C1 ──┬── A1 ────────────────┬── C2 ── C3 ── C4
   │    │    └── B1 ── B2 ──────────┘
   │    │
   │    └─ handler returns a frame
   └─ one name for the frame struct
```

**Do C0 and C1 on both architectures at once.** They are small, mechanical, and symmetric —
this is the opposite of Step 1's advice, and for the opposite reason: there's no unfamiliar
hardware involved, and doing them separately means the shared header disagrees with one arch
for the duration.

**A1 is 30 seconds** and unrelated to everything else. Do it whenever.

**B1 and B2 are the real work of this step.** B2 in particular is a rewrite of `vectors.S`, not
a patch, and it should not be started while anything else is half-finished.

---

## 🧩 C0 — One name for the frame struct ✅

**Goal:** generic code can hold a pointer to a trap frame without knowing what's in it.

Today there are `struct x86_64_registers` (`arch/x86_64/idt.h:33`) and
`struct aarch64_registers` (`arch/aarch64/exceptions.h:6`). The scheduler needs to store one of
these in `struct task` and hand it back to the stub, and it must do that in
architecture-neutral C.

What to do:

- Rename both structs to **`struct trap_frame`**, each in its own arch header. Only one
  architecture is ever compiled at a time, so there is no conflict.
- In `include/arch/arch.h`, add the *incomplete* declaration and the handler prototypes:

```c
// The layout is architecture-specific and deliberately not visible here — generic code only
// ever holds a pointer to one and passes it back to the stub that built it.
struct trap_frame;
```

- Rename the handlers to match (`x86_64_exception_handler` / `aarch64_exception_handler` keep
  their arch prefixes; they're called from `.S` files in the same directory).

**Why an incomplete type rather than a `typedef` or a `void *`:** an incomplete struct type is
exactly the right amount of information. `void *` throws away type checking; a `typedef`
violates the repo's style and hides the pointer-ness; a `#define trap_frame x86_64_registers`
breaks the moment anything forward-declares it. C's incomplete types exist precisely for
"a pointer to a thing whose definition belongs to someone else", and this is that.

**While you're in `exceptions.h`, pin the AArch64 offsets.** `vectors.S` hard-codes `#0`, `#16`,
… `#288` and a frame size of `304`, all of which are the struct's layout re-typed by hand in
another language. Nothing checks that they agree.

```c
_Static_assert(offsetof(struct trap_frame, elr) == 256, "vectors.S frame offsets are stale");
```

…one per field, plus one for the total size against the `304` the stub reserves. It's tedious
once and free forever, and the failure it prevents — add a field, boot, watch the kernel
restore `spsr` from `esr` — is genuinely miserable. (`offsetof` comes in via `stdc.h`'s
`<stddef.h>`.)

> Worth knowing: 31 registers × 8 = 248 bytes, then `sp`@248, `elr`@256, `spsr`@264, `esr`@272,
> `far`@280, `vector_type`@288 → `sizeof` is **296**. The stub reserves **304** because AArch64
> requires `sp` to be 16-byte aligned when it's used as a base address. That 8-byte gap is not
> slack, it's an ABI requirement — worth a comment so nobody "tidies" it.

**Verify:** both architectures build and boot exactly as before. Zero behaviour change.

---

## 🧩 C1 — The stub contract change ✅

**Goal:** the handler returns the frame to restore. This is the context switch, arriving four
steps before there's anything to switch to.

The change is genuinely tiny, which is the whole argument of Chapter 5.

**x86_64** (`isr.S:85-88`): the frame pointer already goes out in `%rdi` and the return value
comes back in `%rax`. One instruction between the call and the pops:

```asm
    movq %rsp, %rdi
    call x86_64_exception_handler
    movq %rax, %rsp          /* restore from whatever frame the handler named */
    popq %r15
    ...
```

**AArch64** (`vectors.S:91-92`): same idea, `x0` in and `x0` out.

```asm
    mov x0, sp
    bl aarch64_exception_handler
    mov sp, x0
```

Then in C, change both handler signatures from `void` to `struct trap_frame *` and make every
existing `return;` into `return regs;`. There are three on x86 (the page-fault resolve, the IRQ
branch) and two on ARM.

**Verify, in two stages.**

First, boot green — returning `regs` unchanged is exactly today's behaviour, so nothing may
move.

Then *prove the stub is really honouring the pointer*, because "it still works" is equally
consistent with having typo'd the instruction into a no-op. Temporarily, in the timer branch
only:

```c
// throwaway: copy the frame somewhere else on this stack and return the copy
struct trap_frame elsewhere = *regs;
return &elsewhere;   // <- yes, a dangling pointer; that is the point, and it works
```

If the kernel keeps ticking, the stub restored from an address it did not choose. Delete it
immediately afterwards — returning the address of a local whose stack frame is about to be
popped is only safe because nothing runs between the `mov sp, x0` and the loads. Don't leave it
in; do run it once.

> [!WARNING]
> On x86_64 the frame you return **must be 16-byte aligned and must be a valid stack**, because
> `iretq` will pop five more words off it. Return a misaligned pointer and the first fault is a
> `#GP` whose reported address is inside perfectly good code. `-Wall` will catch the more likely
> mistake — a missed `return` in a non-void function is a diagnostic, not a silent zero.

---

## 🅰️ A1 — x86_64: take `#PF` off IST1 ✅

**Goal:** page faults run on the stack of whoever faulted, like every other trap.

`idt.c:134` currently reads `(i == 8 || i == 14) ? 1 : 0`. Drop the `i == 14`.

**Why #DF keeps IST1:** a double fault very often *means* the stack is broken. Forcing a
known-good stack is the only way to report anything at all, and a double fault is terminal
anyway so it never needs to nest.

**Why #PF must lose it:** IST does not nest. It forces `rsp` to the same fixed top *every
time*, so a page fault taken while already handling a page fault silently overwrites the outer
frame. Today nothing nests; once demand paging runs inside a preemptible kernel that also
touches user memory, nesting stops being exotic. And page faults are now *routine* — that's
what demand paging is — so routing them through an emergency mechanism was always slightly
wrong.

**Verify:** print `regs` from inside the `int_no == 14` branch, before and after the change. The
`test_vmm()` suite already generates demand-paging faults at boot, so this needs no new code —
just look at where the frame lives. Before: an address inside `x86_64_exception_stack[]`,
identical every time. After: an address on the interrupted stack, and it *varies*.

---

## 🅱️ B1 — AArch64: move the kernel to EL1h ✅

**Goal:** the kernel's stack pointer becomes SP_EL1, leaving SP_EL0 free to mean what the
architecture intends it to mean.

### What's wrong today, precisely

Step 1 established that Limine hands the kernel `SPSel = 0` (EL1t) and nothing has changed it —
which is why current-EL exceptions enter at vector slots 0-3 rather than 4-7. Two consequences
follow that the phase document originally got wrong:

- **`regs->sp` is junk.** Taking an exception to EL1 sets `PSTATE.SP = 1`, so by the time
  `vectors.S:21` runs `mov x3, sp`, the live `sp` is SP_EL1 — a register this kernel has never
  written. The interrupted stack pointer was SP_EL0, and it isn't captured anywhere. The `SP:`
  field in the panic dump (`exceptions.c:77`) has been printing a leftover since it was written.
- **It's harmless only by luck.** `vectors.S:125-126` writes that junk *back* into SP_EL1, which
  nothing reads, and `eret` restores `SPSel = 0` so SP_EL0 is untouched. The whole round trip
  is a no-op on a register nobody uses.

Confirm rather than trust: an unhandled-exception dump prints `SP:` today. Force one (a
deliberate `*(volatile uint64_t *)0x10 = 1;` will do) and look at what it says.

### The change

```c
// arch_init(), before the vector table is installed
// SP_EL1 gets the *same value* sp already has, so the live stack pointer and everything on it
// are unchanged — only which register holds it changes.
uint64_t sp;
asm volatile ("mov %0, sp" : "=r"(sp));
asm volatile ("msr sp_el1, %0" :: "r"(sp));
asm volatile ("msr spsel, #1");
```

**Why this doesn't pull the rug out from under the running function:** the value is identical.
Locals are addressed at offsets from `sp`, the caller's frame is at the same addresses, and
`x29`/`x30` are ordinary registers untouched by the switch. The only thing that changes is
which of two hardware registers the name `sp` resolves to.

Then poison SP_EL0 (`msr sp_el0, xzr`) so that any code path which somehow ends up back at EL1t
faults loudly instead of quietly running on a stale stack pointer.

> `SPSel` is a PSTATE field, written directly rather than through a system register with the
> usual context-synchronisation rules. An `isb` shouldn't be needed the way it is after
> `msr vbar_el1` or `msr cpacr_el1` — but that's exactly the kind of claim worth reading the
> ARM ARM's PSTATE section for rather than taking from this file.

**Verify — and this one is free and decisive.** `exceptions.c:36` currently branches on
`vector_type == 1 || vector_type == 5`. Temporarily narrow it to `== 5`. If ticks still arrive,
current-EL IRQs are now entering the second group of vectors, which means `SPSel` took. Put the
`|| 1` back afterwards (or don't — see B2). `-d int` says the same thing more directly: the
`to EL1 PC` for an IRQ moves from `vector_table + 0x080` to `vector_table + 0x280`.

Also worth re-running with the deliberate fault above: the panic should now say
`Current EL SP_EL1 Synchronous` where it previously said `SP_EL0`, and the demand-pager's data
aborts move from slot 0 to slot 4.

---

## 🅱️ B2 — AArch64: build the frame on the stack the trap arrived on ✅

**Goal:** delete `exception_stack_top`, delete the scratch-register trick, and capture the
interrupted stack pointer correctly. This is the biggest single change in Step 2.

### Why the current stub is shaped the way it is

Worth reconstructing before demolishing it, because the shape is a rational answer to a problem
that is about to disappear.

The stub needs to switch to a known-good stack. To do that it needs a scratch register to hold
the new stack address. But it can't spill a register to save it, *because it doesn't have a
usable stack yet* — that's the whole reason it's switching. So it borrows two system registers
(`tpidr_el1`, `tpidrro_el0`) as spill slots, does the switch, and copies back.

Once the frame is built on the stack the trap arrived on, there is no stack to switch to, no
scratch needed, and the trick evaporates. **All of it — the two `msr`s on entry, the two on
exit, the `ldr x2, =exception_stack_top`, the `.bss` reservation.** The entry becomes:

```asm
.macro VECTOR_ENTRY num
.balign 128
    stp x0, x1, [sp, #-304]!   /* frame is 304 bytes; sp now points at it */
    mov x0, #\num
    b vector_common_stub
.endm
```

Two things this fixes for free:

- **`tpidrro_el0` stops being clobbered.** That register is userland's read-only thread pointer
  — the AArch64 TLS register. Writing it on every trap corrupts thread-local storage the moment
  a libc exists in Phase 4, which is a bug that would surface a whole phase away from its cause.
- **The "read FAR/ESR at cycle 0" comment goes away, and it was a misconception.** `FAR_EL1`
  and `ESR_EL1` are latched by the hardware on exception entry and hold their values until the
  *next* exception. There is no race to win. Read them wherever it's convenient, in the common
  stub with the other system registers.

### Capturing the interrupted stack pointer

This is the one place where the vector slot number genuinely matters, so the macro has to know
it.

| Entry | Interrupted `sp` lives in | Save with | Restore with |
|---|---|---|---|
| Slots 4-7 (current EL, EL1h) | the `sp` you're on, before the frame | `add x?, sp, #304` | nothing — the `add sp, sp, #304` on exit already does it |
| Slots 8-11 (lower EL, AArch64) | `SP_EL0` | `mrs x?, sp_el0` | `msr sp_el0, x?` |

Slots 0-3 are unreachable after B1 (nothing runs at EL1t any more) and 12-15 are AArch32, which
this kernel will never enter. Both groups should now go somewhere that panics loudly rather
than sharing the common path — an unexpected slot is a bug, and Step 1's worst debugging
session was caused by an unrecognised slot falling silently into the wrong decode.

Two macros, or one macro with `.if \num >= 8`, both read fine. The `.if` version keeps the
frame layout in one place, which given how many byte offsets are in this file is worth
something.

> [!IMPORTANT]
> **Never write a lower-EL entry's saved `sp` into SP_EL1.** On a lower-EL return that value is
> the *user's* stack pointer; putting it in SP_EL1 hands userland the kernel's stack pointer for
> the next trap, which is a privilege escalation wearing a typo's clothing. It's not reachable
> until [`PHASE4_PRIVILEGE.md`](../PHASE4_PRIVILEGE.md) Step 1, and it is exactly the sort of thing that gets written now and found much later.

### The exit path

Symmetric with the entry, and one line shorter than today's because the "restore original x0/x1
from scratch registers" dance is gone: `ldp x0, x1, [sp], #304` pops the pair *and* releases the
frame in one instruction, right before `eret`.

Mind the ordering: `elr_el1` and `spsr_el1` must be written back before `eret`, and everything
read out of the frame must be read before the `sp` moves past it.

**Verify:**

1. Boot green on AArch64. Ticks, tests, shutdown.
2. **The frame address varies.** Add a temporary `kprintf` of `regs` in the IRQ branch. Before
   B2 it is the same value on every single tick (`exception_stack_top - 304`); after, it tracks
   whatever the interrupted code was doing. This is the single most convincing check in the
   step.
3. **`regs->sp` is plausible.** Force a fault again — the `SP:` field should now be an address
   in the same neighbourhood as the frame, not a leftover.

---

## 🧩 C2 — Kernel stacks and `arch_set_kernel_stack()` ✅

**Goal:** a task owns its kernel stack, and the CPU can be told about it.

Add to `include/arch/arch.h`, next to the timer/IRQ block Step 1 added:

```c
// Tell the CPU where the kernel stack for the *next* trap from a lower privilege level is.
// Called on every context switch, before returning to a different task's frame.
void arch_set_kernel_stack(void *stack_top);
```

**x86_64:** `sys_tss.rsp0 = (uint64_t)stack_top;`. `sys_tss` is `static` in `idt.c`, so either
the function lives there or the file grows a small setter — the former is less machinery.

**AArch64:** the body is **empty**, and the comment explaining why is the deliverable:

> SP_EL1 *is* the kernel stack pointer, and the return-from-trap path already leaves it at the
> top of whichever task's stack the restored frame came from. There is no separate place for the
> CPU to look it up, so there is nothing to write here.

Trace it once to convince yourself, because an empty function is a claim: the stub does
`mov sp, x0` with the incoming task's frame pointer, reads the frame, and finishes with
`ldp x0, x1, [sp], #304`, which leaves `sp` — that is, SP_EL1 — exactly at that task's kernel
stack top. Then `eret`. The next trap from EL0 lands there. Free.

Now trace the same thing on x86_64 and watch it *not* work: `movq %rax, %rsp`, the pops,
`addq $16, %rsp`, then `iretq` — which pops the user's `rsp` into `rsp` and throws the kernel's
away. That is the asymmetry, and `tss.rsp0` is its patch.

### Allocation

```c
#define TASK_KERNEL_STACK_PAGES 4
#define TASK_STACK_GUARD 0x5041474544414544ULL  // whatever you like; it just has to be unlikely
```

Allocate, write the guard value at the lowest address, and set `kernel_stack_top` to
`base + size` (stacks grow *down*, so the "top" is the high address — a naming trap worth a
comment). Check the guard on every switch and `kpanic` if it's gone.

**Task 0 is special and should not get a fresh stack.** The boot task is already running on the
stack Limine gave it, and it can't be moved off it — you're standing on it. Record the current
`sp` as belonging to task 0 and leave it alone. Its guard value can't be placed, because the
stack's extent isn't known; that's an honest gap, not something to fake.

---

## 🧩 C3 — `struct task` and a scheduler that doesn't schedule ✅

**Goal:** the plumbing exists and runs on every trap, with exactly one task in it.

Start smaller than Chapter 5's sketch. Every field added now is a field that has to be correct in
`fork` later.

```c
struct task {
    uint64_t pid;
    int state;                 // TASK_RUNNING / TASK_READY for now; the rest earn their way in
    void *kernel_stack;        // base, for freeing and for the guard check
    void *kernel_stack_top;    // what arch_set_kernel_stack() gets
    struct trap_frame *frame;  // valid only while suspended
    struct vmm_context *ctx;   // already exists — vmm_create_context()
    struct task *next;
};
```

Deliberately absent: fd table (Phase 5), exit code (Phase 7), priorities, credentials, cwd,
signal state.

The scheduler entry point:

```c
struct trap_frame *sched_on_trap_exit(struct trap_frame *frame) {
    // one task, so this is the identity function today — but the shape is final
    ...
}
```

### Where to call it from

**One place per architecture**, on the way out of the trap path, per Chapter 5's rule. The
existing handlers have several early `return`s, so funnel them: rename the current body to a
`static void` dispatcher and let the exported handler be the single exit point.

```c
struct trap_frame *aarch64_exception_handler(struct trap_frame *regs) {
    aarch64_dispatch(regs);
    return sched_on_trap_exit(regs);
}
```

Set a `need_resched` flag in the timer path rather than rescheduling from inside the IRQ
handler. With one task the flag is never acted on, but establishing "the decision to switch and
the act of switching happen in different places" now is what keeps the kernel
non-preemptible-but-correct in Step 3.

### The task dump

Worth the twenty minutes, because most of the bad days left in Phase 3 are "which task was
running when this happened":

```
[TASK ] pid 0  RUNNING  kstack 0xffff800000abc000-0xffff800000ac0000  used 512/16384  guard ok
```

Call it once after `sched_init()`. Later, call it from the panic path.

**Verify:** boot green, one line of task dump, three ticks, shutdown. Plus one assertion worth
adding while the answer is obvious: the frame pointer arriving at `sched_on_trap_exit()` should
lie inside the current task's kernel stack. For task 0 on the Limine stack you don't know the
bounds, so this only becomes meaningful in Step 3 — write it now anyway, guarded on the task
having a known range.

---

## 🧩 C4 — Prove the per-task stack actually matters ✅

**Goal:** demonstrate the bug that this step fixed, rather than trusting that it did.

Everything above is justified by a failure that doesn't happen today. That's an uncomfortable
place to leave a step, and the demonstration is cheap.

**On AArch64, this is decisive.** Deliberately take a page fault *inside* the timer IRQ handler:
on one specific tick, read from an HHDM address that hasn't been touched yet and sits below
`pmm_get_max_phys_addr()`, so `try_handle_hhdm_mmio_fault()` (`mm/vmm.c:206-219`) resolves it.

- **Before B2**, the nested data abort resets `sp` to `exception_stack_top` and builds its frame
  right on top of the outer IRQ's. The outer frame is destroyed. What that looks like when it
  returns is unpredictable and worth seeing once.
- **After B2**, the nested frame sits below the outer one on the same stack, both complete, and
  the tick after it is normal.

Note the fault only happens on the *first* touch — the demand-pager maps the page and the next
read is a plain load. One shot is enough; pick a different address if you want to repeat it.

Use a **synchronous data abort**, not a second interrupt. Exception entry to EL1 sets
`PSTATE.DAIF` to all-masked, so no IRQ can arrive while the handler runs; a synchronous abort
isn't maskable at all (`PSTATE.A` masks SError, not this), so it fires regardless. That's what
makes the nesting reachable in the first place.

> [!WARNING]
> **The address matters more than this document originally said.** The first draft suggested
> "the GIC's own MMIO window past the registers already in use" — i.e. `0x08001000`, reserved
> space in the GICv2 distributor map. That hangs the machine.
>
> *Reserved in the architecture* and *backed by a device model* are different things. QEMU
> registers the distributor's MMIO region as 0x1000 bytes while `virt` reserves 0x10000 of
> address space for it, so `0x08001000` is unassigned physical memory, and an unassigned access
> on ARM raises a **synchronous external abort** — not a translation fault.
>
> The loop that follows is nasty: `esr_ec` is still `0x25`, so `aarch64_dispatch` calls
> `vmm_handle_page_fault`, and `try_handle_hhdm_mmio_fault()` checks only the *address range*,
> never the fault reason. It maps the page, returns true, the CPU `eret`s and re-executes the
> load — which aborts again, because the page was never the problem. Forever, with IRQs masked.
>
> Use an address a real device model answers. **virtio-mmio slot 0 at `0x0A000000`** is the
> clean choice on `virt`: all 32 slots exist even when unpopulated, offset 0 is the magic
> register, the read has no side effects, and it returns the recognizable constant `0x74726976`
> (`"virt"`) — so the printed value itself proves the load reached a device rather than a zero
> that could mean anything.
>
> And **count the aborts taken while probing, panicking on the second.** A deliberate-fault
> experiment must not be able to spin. That one counter is the difference between a legible
> failure and a silently wedged machine — which is exactly the failure mode Step 1's worst
> debugging session was made of.

### Observed

```
[C4] outer IRQ frame at   FFFF0000BA91EE50, elr 0xffffffff800184b8
[C4] nested abort frame at FFFF0000BA91ECB0, far 0xffff00000a000000, ec 0x25
[C4] probe read 0x74726976 (expected magic), resumed inside the IRQ handler
[C4] outer elr now        0xffffffff800184b8 -> UNCHANGED, outer frame survived
[TIMER] 1 / 2 / 3, clean shutdown
```

The two frames are **416 bytes apart, nested below outer** — 304 of `TRAP_FRAME_SIZE` plus 112
of C frames for `aarch64_exception_handler` → `aarch64_dispatch`. Nothing unexplained in the
gap. Both sit on task 0's boot stack, whose dump reported `sp was 0xffff0000ba91ef40`, 240
bytes above the outer frame.

**Two distinct addresses is the whole proof.** Under the pre-B2 stub both lines would have
printed `exception_stack_top - 304` — the same address twice, because every trap reset `sp` to
the one global stack. The outer `elr` surviving unchanged is the second half: distinct
addresses show the frames didn't collide, the unchanged `elr` shows the outer trap's *contents*
came through and it resumed correctly.

This also means the "before" case never had to be rebuilt. The old bug's signature is an
equality between two numbers the new run prints, so the post-fix run alone is sufficient
evidence — worth remembering the next time a step's acceptance criterion is an absence.

**On x86_64 there is no equally clean demo**, and it's better to say so than to invent one. The
equivalent failure is a `#PF` nested inside a `#PF`, both on IST1, and staging that deliberately
is more work than the fix. A1's before/after print of the frame address is the honest check:
page faults demonstrably stopped landing on the emergency stack.

Delete all of this once it has been seen. It is a demonstration, not a test — a self-test that
deliberately faults inside an interrupt handler is a self-test that hangs the build the day
something regresses.

---

## 🔬 Debugging tools worth knowing before you need them

Step 1's list still applies (`-d int`, `-d int,cpu_reset`, `-D logs/…`, the QEMU monitor). Two
more become relevant here, because this step's failures are stack failures:

- **`info registers` in the QEMU monitor** shows `SP_EL0`/`SP_EL1` separately on AArch64, and
  `rsp` plus the TSS on x86. This is the fastest way to answer "which stack am I actually on",
  which is the central question of the entire step.
- **`nm kernel/bin/kernel-$(ARCH) | sort`** to get the boot stack's neighbourhood and the
  `.bss` extents. When a frame address looks wrong, the useful question is "wrong compared to
  what", and that needs the map.

The single most valuable habit for this step: **print the address of `regs`.** It costs one
`kprintf` and it answers "which stack, and did it move" directly, which is what almost every
failure below reduces to.

---

## 🕳️ Failure modes, collected

Grouped by symptom.

**AArch64 dies immediately at boot, no output.**
- B1: `msr spsel, #1` executed before SP_EL1 was seeded, so the kernel is running on whatever
  SP_EL1 held. Order matters: seed, *then* switch.
- B1: the two `asm volatile` blocks got reordered or merged by the compiler. Put the seed and
  the switch in one asm block if there's any doubt.

**AArch64 boots, then silence the moment interrupts are enabled.**
- B1's verification narrowed the IRQ branch to `vector_type == 5` and `SPSel` didn't actually
  take, so IRQs are still arriving at slot 1 and falling through to the `ESR_EL1` decode — which
  holds a stale syndrome, which `vmm_handle_page_fault()` cheerfully "resolves". Silent storm.
  This is Step 1's worst bug, reachable again by exactly this route. `-d int` splits it in one
  run.

**Random corruption, or the kernel restores nonsense after a trap.**
- B2: the byte offsets in `vectors.S` disagree with `struct trap_frame`. This is what C0's
  `_Static_assert`s exist to prevent — if you skipped them, this is the bill.
- B2: frame size not a multiple of 16, so `sp` is misaligned and every `stp` with an `sp` base
  faults or misbehaves.

**x86_64 triple-faults right after the contract change (silent reboot loop).**
- C1: the handler returned something that isn't a valid, 16-byte-aligned stack — most likely a
  missed `return regs;` returning zero. `-Wall` flags the missing return; `-d int,cpu_reset`
  confirms the triple fault.

**A `#GP` whose reported address is inside code that is obviously fine.**
- C1 on x86: misaligned returned frame, discovered by `iretq` rather than by the instruction
  that caused it.

**Everything works, but the guard value trips.**
- Kernel stack overflow — believe it. 16 KiB is not much once a recursive page-table walk and a
  nested trap are both on it. Print the high-water mark from the task dump before assuming the
  guard check is wrong.

**Everything works and nothing at all is different.**
- That's the acceptance criterion. But run C1's throwaway frame-copy and B2's frame-address
  print before believing it — "no change" and "the change is a no-op" are indistinguishable
  from the boot log alone, and this is the one step where they're genuinely easy to confuse.

---

## 📁 Files touched

- ✅ `kernel/include/arch/arch.h` — `struct trap_frame;` incomplete declaration,
  `arch_set_kernel_stack()`
- ✅ `kernel/arch/x86_64/idt.h` — `struct x86_64_registers` → `struct trap_frame`
- ✅ `kernel/arch/x86_64/idt.c` — handler returns a frame, `#PF` off IST1,
  `arch_set_kernel_stack()` writing `sys_tss.rsp0`, dispatch split from the exit point
- ✅ `kernel/arch/x86_64/isr.S` — `movq %rax, %rsp` after the call
- ✅ `kernel/arch/aarch64/exceptions.h` — rename. The offsets `vectors.S` hard-codes were
  going to be pinned here with `_Static_assert`s; instead they moved to a shared
  `kernel/arch/aarch64/trap_frame.h` that both this header and `vectors.S` include, so there
  is nothing left to assert against
- ✅ `kernel/arch/aarch64/exceptions.c` — handler returns a frame, dispatch split from the exit
  point, IRQ branch narrowed to the EL1h slot
- ✅ `kernel/arch/aarch64/vectors.S` — **rewritten**: no global stack, no scratch-register
  trick, correct `sp` capture per entry group, `mov sp, x0` after the call, loud panic for
  unreachable slots
- ✅ `kernel/arch/aarch64/arch.c` — seed SP_EL1 and `msr spsel, #1` in `arch_init()`, poison
  SP_EL0, empty `arch_set_kernel_stack()` with the invariant written down
- ✅ `kernel/proc/task.c` + `kernel/include/proc/task.h` (new) — `struct task`, kernel stack
  allocation, guard value, task dump
- ✅ `kernel/proc/sched.c` (new) — `sched_init()`, `sched_on_trap_exit()`, `need_resched`
- ✅ `kernel/Makefile` — add `proc` to the `find` on line 30, *if* the new directory route is
  taken
- ✅ `kernel/core/main.c` — `sched_init()` before `arch_irq_enable()`, one task dump

Deliberately **not** touched: `fs/`, `mm/`, the GDT, `syscalls.h`. If a change wants to reach
into those, it belongs to Phase 4 or later.

---

## 🪜 Verification

`core/test/` can reach almost none of this, for the same reason as Step 1: a trap path is not
callable from a self-test. Two exceptions worth adding, since they're pure functions of data:

- **The guard-value check** — write a stack, corrupt the guard, confirm the checker notices.
- **Kernel stack allocation and free** — allocate N, free them, confirm
  `pmm_get_free_page_count()` returns to where it started. The existing PMM tests already
  establish that pattern.

Both got written, as `core/test/test_kstack.c` — five assertions under the `[KSTK]` tag,
covering alloc, page alignment, guard-intact, guard-stomped, and free-returns-every-page. They
are the only permanent test coverage this step produced, and they pass on both architectures.

Everything else is the boot log, and this step's boot log is supposed to be boring:

| Check | Expected | Result |
|---|---|---|
| Both arches, normal boot | identical to before, plus one task-dump line | ✅ tests pass, 3 ticks, clean shutdown |
| `[KSTK]` self-tests | 5 × PASS | ✅ both arches |
| C1's throwaway frame copy | ticks continue | ✅ stub honours the returned pointer |
| A1 frame address (x86, `#PF`) | outside `x86_64_exception_stack[]`, and varies | ✅ |
| B1 narrowed IRQ branch | ticks continue with `vector_type == 5` alone | ✅ — it is now permanently narrowed to 5 |
| B2 frame address (ARM, IRQ) | varies between ticks | ✅ |
| B2 forced fault | `Current EL SP_EL1 Synchronous`, plausible `SP:` | ✅ |
| C4 nested fault (ARM) | outer trap resumes, next tick normal | ✅ frames 416 bytes apart, outer `elr` unchanged |

All throwaway instrumentation (C1's frame copy, A1/B2's address prints, C4's probe) was deleted
after being observed. Nothing from it is in the tree.

One thing worth carrying forward rather than doing here: once Step 3 has two tasks, the
frame-inside-my-own-kernel-stack assertion from C3 becomes a real invariant with a real chance
of firing. Write it now, believe it then.

---

## ➡️ What Step 3 inherits

Three things this step established that Step 3 leans on directly, worth naming so they aren't
rediscovered:

- **`sched_on_trap_exit()` is the only place a switch may happen**, and `need_resched` is
  already set by the timer path. Step 3's work is to make `sched_pick_next()` return something
  other than `current` — the mechanism around it is done and running on every trap today.
- **The exit path reads the vector group out of the frame** (see B2 above), so a frame
  belonging to a *different* task returns to the right stack pointer. That was the one place
  the design's entry-time `.if` would have quietly done the wrong thing.
- **Task 0 has no stack guard and no known extent**, because it runs on Limine's stack. Task 1
  will be the first task with a real `kstack_alloc()` stack, which is also the first time the
  guard check in `sched_on_trap_exit()` has anything real to check.
