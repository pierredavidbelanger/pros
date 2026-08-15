# Working Document: Phase 4 Step 1 — Ring 3 / EL0, with a Hand-Fabricated User Program [STATUS: NOT STARTED ⬜]

> [!NOTE]
> **Phase 4 Step 1**, from [`PHASE4_PRIVILEGE.md`](PHASE4_PRIVILEGE.md) Chapter 3, expanded into
> individually verifiable Parts. It draws on Chapter 1 (privilege levels and the trick of
> entering userland) and Chapter 5 (the traps). Read those for the *why*; this document is the
> *how*, in order.
>
> It also leans hard on [`archive/PHASE3_PREEMPTION.md`](archive/PHASE3_PREEMPTION.md) Chapter 1:
> **the trap frame is the process.** If that idea isn't comfortable yet, this step will not make
> sense — every single thing here is "fabricate a frame and let the existing stub restore it".

> [!NOTE]
> Mentor-mode reminder (see the note at the top of [`../README.md`](../README.md)): this is a
> design reference to code from by hand, not code to paste in. Signatures, struct fields and
> constants are given exactly, because guessing those wastes time without teaching anything —
> but the bodies are yours. Where a register value or a hardware behaviour is stated, confirm it
> against the manual; Steps 1, 2 and 3 of Phase 3 all have corrections that exist because a
> draft was trusted.

---

## 🎯 What "done" looks like

> On both architectures, a few hand-assembled instructions sitting in a page the kernel mapped
> as user-accessible get scheduled like any other task, execute at **ring 3 / EL0**, and then
> **fault** on a privileged instruction. The unhandled-exception dump names the fault and shows
> a user code selector (x86_64) or an EL0 `SPSR` (AArch64).

**The fault is the success criterion.** That is genuinely strange the first time: every previous
step ended with a clean boot, and this one ends with a register dump. But a privileged
instruction that *doesn't* fault means the privilege drop never happened — the blob ran happily
in ring 0 and nothing was proven. The crash is the evidence.

> [!NOTE]
> **This step's acceptance criterion is a panic, which used to be exactly what the logging
> pipeline ate — that is now fixed.** `kpanic()` ends in `arch_halt()`, which never returns, so
> QEMU never exits and the pipe is never closed; a buffering filter in that pipe would lose the
> last few KB, including the dump you are trying to read, the moment you killed the run. That
> bit Phase 3 Step 3's deliberate-break test and cost real time.
>
> `ansifilter` has since been removed from all four QEMU targets (see the closed entry in
> [`SIDEQUEST.md`](SIDEQUEST.md)), so `logs/qemu-<arch>.log` now keeps everything up to the
> halt. Worth knowing anyway: **if you ever put a filter back in that pipe, this step is where
> it will hurt first.**

**What is explicitly *not* in this step:**

- ❌ **No syscalls.** No `syscall`/`sysret`, no `svc` dispatch, no `write`. That is Step 2, and
  it is the phase's actual payoff. Here, userland has no way to *ask* the kernel for anything —
  it can only run, and fault.
- ❌ **No ELF loader.** The program is a byte array. Phase 5.
- ❌ **No per-task address space.** The user page is mapped into the *kernel* context and
  `task->ctx` stays unused, exactly as in Phase 3. `vmm_switch_context()` is still not called
  from the scheduler. Phase 5.
- ❌ **No user memory validation.** Nothing crosses the boundary yet, so there is nothing to
  validate. `copy_from_user` is Phase 5.
- ❌ **No task exit, no reaping, no signals.** The user task faults and the kernel panics. Making
  a fault kill *the task* rather than *the machine* needs process lifetime, which is Phase 7.
- ❌ **No SMAP.** It pairs with `copy_from_user` (Phase 5). SMEP is cheap and optional here —
  see A2.

---

## 🧠 The mental model, before any code

### There is no "enter userland" instruction

This is the part worth internalising before writing anything, because it makes the step much
smaller than it sounds.

There is no `goto_ring3`. Dropping privilege is a **side effect of returning from a trap that
never happened**. `iretq` pops `cs`, and if the popped `cs` has RPL 3, the CPU is now in ring 3.
`eret` restores `SPSR_EL1`, and if `SPSR.M[3:0]` says EL0t, the CPU is now at EL0. That is the
whole mechanism.

Which means the code that enters userland already exists and has been running since Phase 3
Step 2: it is the trap stub's exit path. Step 3 taught it to restore a frame that no trap ever
wrote (`arch_task_init_frame`), and every kernel thread in the tree starts that way today. This
step changes **five fields in that fabricated frame** and userland happens.

```
   Phase 3 Step 3 — kernel thread          Phase 4 Step 1 — user task
   ────────────────────────────────        ─────────────────────────────────
   x86:  cs = 0x08  (ring 0)               x86:  cs = user code | 3
         ss = 0x10                               ss = user data | 3
         rsp = kernel stack                      rsp = USER stack
   arm:  spsr = 0x5 (EL1h)                 arm:  spsr = 0x0 (EL0t)
         vector_type = 5 (current EL)            vector_type = 9 (LOWER EL)
         sp = cosmetic                           sp = USER stack, and load-bearing

   ...restored by the exact same stub, which cannot tell the difference.
```

### The frame is a promise the hardware keeps

The kernel writes a frame saying "this task was interrupted while running unprivileged, at this
address, on this stack". The stub restores it. The CPU has no way to know that moment never
happened — and no reason to care.

That is also the answer to the question Chapter 1 leaves you with: *what stops a user process
from executing `iretq` itself with a ring-0 `cs`?* `iretq` is not privileged, and userland can
absolutely execute it. But `iretq` **cannot raise privilege**: returning to a `cs` with numerically
lower RPL than the current privilege level is a `#GP`. Privilege can only drop on the way out of
a trap, and only the trap entry — which the hardware controls — can raise it.

### Why the fault, specifically, proves it

When the blob executes its privileged instruction, the CPU takes an exception. The frame the
stub builds records **where it came from**: `cs` on x86_64, `SPSR_EL1.M[3:0]` on AArch64. The
existing panic dump already prints both. So the evidence is not "it crashed" — anything can
crash — it is "it crashed *and the frame says the faulting code was unprivileged*".

---

## ⚖️ Four decisions to make before writing anything

### 1. Which address space does the user page live in?

**Recommendation: the kernel context (`vmm_kernel_context`), not a new one.**

`vmm_create_context()` exists and works, but nothing calls `vmm_switch_context()` from the
scheduler, and wiring that in is a genuine capability (per-process address spaces) that Phase 5
owns. Mapping one user page into the context that is already active costs nothing and keeps this
step to exactly one new idea.

It works on both architectures for the same reason, which is worth tracing once:
`get_root_table()` (`mm/vmm.c:110`) sends any address below `VMM_ADDR_SPLIT` to
`ctx->root_virt`, and `vmm_kernel_context->root_virt` is the low-half root on both — CR3's table
on x86_64, and the blank TTBR0 table `arch_vmm_ensure_user_root()` allocated on AArch64. A low
address is a low address.

### 2. Where in the address space?

**Recommendation: a named constant at `0x0000_0000_4000_0000` (1 GiB), code page and stack page
adjacent.** Far from zero, so a null dereference still faults instead of hitting real mapping;
comfortably inside the low half; page-aligned; and memorable in a dump.

```
0x40000000  ┌────────────────┐  code page  (user, executable)
            │  blob bytes    │
0x40001000  ├────────────────┤  stack page (user, writable)
            │       ↓        │  ← user stack grows down from 0x40002000
0x40002000  └────────────────┘
```

Two pages, not one. Sharing them would work in this step (the blob never pushes), but a user
stack that overlaps user code is the kind of shortcut that costs an hour the moment Step 2's
`syscall` starts pushing.

### 3. How is the blob built?

**Recommendation: a `static const uint8_t` array in `main.c`, with the assembly in a comment
above it.** No new build steps, no linker script, no `objcopy` — and it all gets deleted in
Phase 5 when a real ELF replaces it. Verify the bytes with the toolchain rather than trusting
any listing, including this one:

```bash
# write the instructions to a .S, then:
zig cc -target x86_64-freestanding-none -c blob.S -o blob.o && objdump -d blob.o
```

The encodings below were produced that way against this repo's own toolchain:

| Arch | Assembly | Bytes (in memory order) |
|---|---|---|
| x86_64 | `hlt` | `F4` |
| x86_64 | `jmp .` (back 2) | `EB FD` |
| AArch64 | `mrs x0, sctlr_el1` | `00 10 38 D5` |
| AArch64 | `b .` | `FF FF FF 17` |
| AArch64 | `svc #0` | `01 00 00 D4` |
| AArch64 | `nop` | `1F 20 03 D5` |

### 4. What should the blob actually do?

**Recommendation: privileged instruction first, then an infinite loop.**

```
    hlt          /  mrs x0, sctlr_el1     ← must fault; this is the whole test
    jmp .        /  b .                   ← backstop: if it did NOT fault, hang here
```

The loop matters. If the privileged instruction somehow *succeeds*, execution runs off the end
of the page into unmapped memory and you get a page fault at `0x40000002` — which looks like a
mapping bug and isn't one. With the loop, "didn't fault" presents as a clean hang at a known
address, which is unambiguous. Two different failures, two different symptoms, no guessing.

`svc #0` is deliberately **not** the instruction to use here even though it also traps from EL0:
it is Step 2's entry path, and using it now conflates "did the privilege drop work" with "is the
syscall path wired", which is exactly the pair Chapter 3 split into two Steps to keep apart.

---

## 🗺️ Suggested order

Parts are labelled by track — **C** architecture-neutral, **A** x86_64, **B** AArch64.

```
  C0 ── C1 ──┬── A1 ──┬── C2 ── C3
             └── B1 ──┘         │
   │    │         │      │      └─ prove it's really unprivileged
   │    │         │      └─ THE DROP: schedule it and watch it fault
   │    │         └─ fabricate an unprivileged frame (per arch)
   │    └─ a task that owns user memory, not scheduled yet
   └─ a user page with something in it, no privilege change at all

  A2 ── SMEP, any time after A1 (x86_64 only, ~5 lines)
```

Same shape as Phase 3 Step 3, deliberately: **everything before C2 is verifiable while the
machine is still entirely in ring 0.** You build the page, then the task, then its frame, and
inspect each one from the kernel — where a mistake is a printf away from being obvious — before
handing control to code that cannot be debugged by printing.

---

## 🧩 C0 — A user page, with something in it ⬜

**Goal:** two pages mapped user-accessible at a known address, the blob copied in, verified by
reading it back. **Nothing changes privilege level.** The boot log gains two lines.

Everything here uses APIs that already exist and are tested.

```c
// core/main.c
#define USER_BASE       0x0000000040000000ULL
#define USER_CODE_ADDR  USER_BASE
#define USER_STACK_ADDR (USER_BASE + PAGE_SIZE)
#define USER_STACK_TOP  (USER_STACK_ADDR + PAGE_SIZE)
```

For each of the two pages: `pmm_alloc(1)`, then `vmm_map_page(vmm_kernel_context, virt, phys,
VMM_USER | VMM_WRITABLE)`. Write the blob through the **HHDM** alias (`pmm_phys_to_virt(phys)`),
not through `USER_CODE_ADDR` — both work today, but writing through the kernel's own view of the
physical page is the habit that survives Phase 5, when the user address is only mapped in a
context that isn't active while the loader runs.

### The flag that isn't there, and why that's correct

There is no `VMM_EXECUTABLE`. Executability is the *absence* of `VMM_NO_EXECUTE`, so a page
mapped without it is executable — which is what the code page needs. The stack page would ideally
be non-executable (`VMM_NO_EXECUTE`), and that's worth doing simply because it costs one flag.

### The trap that already got handled for you

A user-accessible leaf is not enough on x86_64: **every intermediate table entry on the walk must
also have the user bit**, or the CPU denies the access before it ever reads the leaf. This is a
classic multi-hour bug, and it cannot bite here — `get_or_create_table()` (`mm/vmm.c:120`) already
installs every table it creates with `VMM_PRESENT | VMM_WRITABLE | VMM_USER`. Worth reading that
line once and moving on, rather than discovering it later from the wrong direction.

On AArch64 the same concern is a non-issue for a different reason: table descriptors carry
`APTable`/`UXNTable`/`PXNTable` restriction bits which `arch_vmm_make_pte()` leaves at zero
(`is_table` returns early), meaning "no restriction added" — permissions come from the leaf.

**Verify:** print `vmm_virt_to_phys(vmm_kernel_context, USER_CODE_ADDR)` and confirm it matches
the physical page you allocated, then read the first bytes back through `USER_CODE_ADDR` and
confirm they're the blob. Both reads happen in ring 0 where everything is permitted, so this
proves *mapping*, not *permission* — that's C2's job. Boot is otherwise unchanged and still
shuts down cleanly.

---

## 🧩 C1 — A task that owns user memory ⬜

**Goal:** a `struct task` describing a user program exists, fully formed, and is never scheduled.

The kernel-thread path stays untouched. A separate constructor keeps the two cases honest:

```c
// proc/task.h

// Create a task that will start executing at USER privilege (ring 3 / EL0).
// `user_entry` and `user_stack_top` are addresses in the task's *user* address space —
// they are not kernel pointers and must never be dereferenced by the kernel, which is
// why they are uint64_t rather than pointers.
// Allocates a kernel stack (for traps arriving from userland) and fabricates the initial
// frame via arch_task_init_user_frame(). Not added to the run queue.
struct task *task_create_user(const char *name, uint64_t user_entry, uint64_t user_stack_top);
```

### Two stacks, and why the kernel one still matters

A user task has **two** stacks, and conflating them is the single most common confusion in this
step:

| Stack | Where | Who uses it | Set by |
|---|---|---|---|
| **user stack** | `USER_STACK_TOP`, user-accessible | the blob, at ring 3 / EL0 | `frame->rsp` / `frame->sp` |
| **kernel stack** | `kstack_alloc()`, kernel-only | the trap stub, when userland traps | `arch_set_kernel_stack()` |

`task_create_user()` still calls `kstack_alloc()` exactly like `task_create()` does. That stack is
empty while the task runs in userland, and it is where the CPU will build the frame the instant
the blob faults.

> [!IMPORTANT]
> **This is where Phase 3's dormant `tss.rsp0` wiring finally matters, and where
> [`PHASE4_PRIVILEGE.md`](PHASE4_PRIVILEGE.md) Chapter 5's second bullet becomes real.**
>
> `arch_set_kernel_stack()` writes `tss.rsp0` on every context switch and has done nothing
> observable since Step 2, because the CPU only consults `rsp0` on a trap **from a lower
> privilege level** — and there has never been one. This step creates the first.
>
> The hazard named there: task 0's `kernel_stack_top` points *inside Limine's live boot stack*,
> so if the task returning from EL0 were task 0, the CPU would build its trap frame on top of
> whatever C locals were mid-flight, and corrupt them silently. It isn't a triple fault, which is
> what makes it nasty. The fix is automatic here as long as the user task comes from
> `task_create_user()` (which allocates a real stack) rather than being bolted onto task 0 — but
> it is worth knowing *why* that's a rule and not a preference.

**Verify:** `task_create_user()` in `main.c`, then `task_dump_all()`. Three lines, the new task
`READY` with a real `kstack` range and `guard ok`. Nothing is scheduled, so the boot log is
otherwise C0's. Then `task_destroy()` it immediately and check `pmm_get_free_page_count()`
returns to where it started — the same pattern `test_task.c` already uses.

---

## 🅰️ A1 — x86_64: user segments, and an unprivileged frame ⬜

**Goal:** a GDT that can describe ring 3, and a frame that names it.

### The GDT, laid out for a constraint that doesn't apply yet

The current table (`idt.c:33-38`) is:

```
0x00  null
0x08  kernel_code      ─┐ syscall computes SS = CS + 8, so these two
0x10  kernel_data      ─┘ must stay adjacent (already true)
0x18  tss_entry         ← 16 bytes, so it occupies 0x18 AND 0x20
```

> [!IMPORTANT]
> **Lay the user entries out to `sysret`'s arithmetic now, even though nothing enforces it until
> Step 2.** `iretq` consumes whatever selectors it is handed and does not care where they sit.
> `sysret` does not read the GDT at all — it *computes*: `SS = STAR[63:48] + 8` and
> `CS = STAR[63:48] + 16`, both with RPL forced to 3. Place them anywhere convenient now and
> they get renumbered in Step 2, invalidating every constant and every dump you learned to read.

Appending after the TSS satisfies it, and leaves `ltr $0x18` (`idt.c:111`) alone:

```
0x28  (reserved)       ← STAR user base points here; sysret never loads this slot in 64-bit
                         mode (it is the 32-bit compat CS). Leave it zero.
0x30  user_data        ← STAR base + 8   → SS = 0x33
0x38  user_code        ← STAR base + 16  → CS = 0x3b
```

The descriptor bytes are the kernel ones with DPL 3 — `access |= 0x60` — which is worth deriving
rather than copying: `0x9A | 0x60 = 0xFA` for code, `0x92 | 0x60 = 0xF2` for data, granularity
bytes unchanged (`0xAF` / `0xCF`).

```c
// arch/x86_64/idt.h
#define X86_64_SELECTOR_USER_DATA 0x33   // gdt.user_data (0x30) | RPL 3
#define X86_64_SELECTOR_USER_CODE 0x3b   // gdt.user_code (0x38) | RPL 3
#define X86_64_STAR_USER_BASE     0x28   // Step 2 will need this; the layout above is built for it
```

> [!WARNING]
> **These are not Linux's numbers, and [`PHASE4_PRIVILEGE.md`](PHASE4_PRIVILEGE.md)'s verification
> table says `CS=0x33` because it quotes Linux's.** Linux puts its TSS *after* the user entries
> and lands on `__USER_CS = 0x33` / `__USER_DS = 0x2b`. This tree's TSS sits at 0x18, so
> everything shifts by one slot: **`CS = 0x3b`, `SS = 0x33`**. Both layouts are correct; only one
> of them is what your dump will print. Compute yours and write it down before C2, or you will
> spend C2 wondering whether a correct dump is wrong.
>
> Matching Linux exactly is a legitimate alternative — it means moving the TSS to 0x38 and
> updating `ltr`. Pick one deliberately.

### The frame

```c
// include/arch/arch.h
struct trap_frame *arch_task_init_user_frame(void *kernel_stack_top,
                                             uint64_t user_entry,
                                             uint64_t user_stack_top);
```

Implement it beside `arch_task_init_frame()` in `arch/x86_64/arch.c` (Step 3 put frame
fabrication there rather than in `idt.c`; stay consistent).

It is Step 3's function with three fields changed, and one thing removed:

- `rip` = `user_entry`, `rsp` = `user_stack_top` — both **user** addresses now.
- `cs` = `X86_64_SELECTOR_USER_CODE`, `ss` = `X86_64_SELECTOR_USER_DATA`. **The RPL bits in these
  selectors are what drop the privilege level.** Nothing else does.
- `rflags` = `X86_64_RFLAGS_RESERVED_BIT1 | X86_64_RFLAGS_IF` — unchanged, and `IF` is still
  mandatory for the same reason: a user process with interrupts masked can never be preempted,
  and looks exactly like a scheduler bug.
- **No `task_exit_guard` slot.** Step 3 planted a kernel function pointer at the top of the
  kernel stack so a stray `ret` landed somewhere legible. Doing that here would put a *kernel
  address* on a *user* stack, which is both useless (userland can't jump there — it would fault,
  which is correct but confusing) and a small information leak of the kernel's layout. Leave the
  user stack empty; a stray `ret` pops zero and faults at address 0, which is perfectly legible.

The 16-byte alignment reasoning from Step 3 A1 still applies to where the *frame* sits on the
kernel stack. The user stack pointer wants the same SysV treatment (`rsp ≡ 8 (mod 16)` at entry)
in principle — irrelevant for a blob that never calls anything, and free to get right anyway.

**Verify without entering it:** print the fabricated frame's `rip`/`cs`/`rflags`/`rsp`/`ss` and
check `cs` and `ss` against the constants above, then confirm `(cs & 3) == 3`. That last check is
the one that matters: RPL 3 is the privilege drop, in one bit pair.

---

## 🅱️ B1 — AArch64: EL0t, and the field that finally earns its keep ⬜

**Goal:** the same thing for `eret`, which is shorter and has one genuinely elegant detail.

Implement beside `arch_task_init_frame()` in `arch/aarch64/arch.c`.

```c
#define AARCH64_SPSR_M_EL0T 0x0ULL         // M[3:0] = 0b0000: return to EL0, using SP_EL0
#define AARCH64_VECTOR_LOWER_EL_IRQ 9      // vector slot 9: lower EL, AArch64, IRQ
```

- `frame->elr` = `user_entry`
- `frame->spsr` = `AARCH64_SPSR_M_EL0T` — `0x0`. Every DAIF bit clear, so interrupts are enabled
  at EL0 (the exact analogue of x86's `IF`, with the same failure mode if you get it wrong).
  Compare with the kernel thread's `0x5` (EL1h): **the entire privilege drop is those three
  bits.**
- `frame->sp` = `user_stack_top`
- `frame->x[30]` = `0` — same reasoning as A1's missing guard slot: no kernel address on a user
  task's LR.
- `frame->x[0..29]`, `esr`, `far` = 0.
- `frame->vector_type` = `AARCH64_VECTOR_LOWER_EL_IRQ` — **and this is the interesting one.**

> [!IMPORTANT]
> **`vector_type` stops being cosmetic and becomes the mechanism.**
>
> Step 2 Part B2 discovered that *which stack pointer a frame returns to is a property of the
> frame, not of the trap that arrived*, and built the exit path to read `vector_type` back out of
> the frame: `< 8` leaves `SP_EL1` alone, `>= 8` does `msr sp_el0, frame->sp`
> (`vectors.S:95-104`).
>
> Step 3 set it to `5` for kernel threads, which takes the `< 8` branch — the frame's `sp` field
> was documented there as "ignored by the exit path, cosmetic". **Here it is the opposite.**
> Setting a lower-EL slot is what makes the exit path install the user stack pointer into
> `SP_EL0` before `eret`. Without it, `SP_EL0` keeps whatever it had and the blob's first `push`
> faults.
>
> And what it had is not garbage — `arch_init()` deliberately poisons it: `msr sp_el0, xzr`
> (`arch/aarch64/arch.c:45`), with the comment "nothing should ever run at EL1t again, and a path
> that does will fault on its first push". That poison is now doing a second job: if you forget
> `vector_type`, the blob faults on a **null stack pointer**, which is a loud, unambiguous
> symptom rather than a mysterious one.
>
> This is the single most satisfying thing in the step: two decisions made two Steps apart, for
> unrelated reasons, combine into "the right thing happens, and the wrong thing is obvious".

Use `TRAP_FRAME_SIZE` (304), not `sizeof(struct trap_frame)` (296), for the same reason as Step 3
B1 — the exit path releases the frame with `ldp x0, x1, [sp], #TRAP_FRAME_SIZE`.

### The gap this reveals, and why it's being left open

A user *code* page must be executable at EL0. `arch_vmm_make_pte()` sets `UXN` and `PXN` together,
only when `VMM_NO_EXECUTE` is passed — so a page executable at EL0 is also executable at EL1.
That is the AArch64 equivalent of the hole SMEP closes on x86 (A2), and closing it properly needs
a `VMM_USER_EXEC`-shaped flag that can set `PXN` without `UXN`.

**Recommendation: note it, don't fix it here.** It's a VMM flag-vocabulary change, it affects
both architectures' `arch_vmm_make_pte()`, and it has no observable effect until something can
trick the kernel into jumping at a user address. Record it as an open item so it's a decision
rather than an oversight.

**Verify:** print the fabricated frame's `elr`/`spsr`/`sp`/`vector_type`. Confirm
`(spsr & 0xF) == 0` (EL0t) and `vector_type >= 8`. As in A1, this is checkable at rest, before
anything unprivileged has run.

---

## 🧩 C2 — The drop ⬜

**Goal:** run it. This is the Part where the machine stops booting cleanly, on purpose.

Everything is built. Add the task to the run queue, before `arch_irq_enable()`, exactly like
Step 3's kernel threads:

```c
// core/main.c
struct task *user = task_create_user("user", USER_CODE_ADDR, USER_STACK_TOP);
if (!user) kpanic("task_create_user failed");
sched_add_task(user);
task_dump_all();
...
arch_irq_enable();
```

Nothing else changes. `sched_on_trap_exit()` does not need to know this task is special — it
picks it, calls `arch_set_kernel_stack()` (which now finally matters on x86_64), and returns its
frame. The stub restores it. The privilege drop happens inside `iretq`/`eret`, one instruction,
with no code of yours involved.

**Expected output, and how to read it:**

```
[TASK ] pid 3  user  READY  kstack 0x...-0x...  guard ok
[IRQ  ] Enable IRQ
[K    ] Count to 3:
[T1   ] 50
[X8664] ============= [ UNHANDLED EXCEPTION ] =============
[X8664]  Exception 13: General Protection Fault (#GP)
[X8664]  RIP: 0x0000000040000000  CS: 0x003b  RFLAGS: 0x...
[X8664]  RSP: 0x0000000040002000  SS: 0x0033
...
[PANIC]
```

Three things to check, in order of how much they prove:

1. **`RIP` / `ELR_EL1` is `0x40000000`** — the blob's address. The CPU really did execute the
   bytes you copied.
2. **`CS` is `0x3b` (or your computed value) / `SPSR_EL1`'s low nibble is `0`** — the frame says
   the faulting code was **unprivileged**. This is the actual acceptance criterion.
3. **The exception is the one your instruction should cause** — `#GP` (13) for `hlt` on x86_64;
   `EC = 0x18` (trapped MSR/MRS) on AArch64. A *different* exception means something else went
   wrong first, and the privilege drop may not be what you're looking at.

`RSP`/`SP` should be the user stack top, untouched, since the blob never pushed.

> [!NOTE]
> The countdown will not reach 3 and the machine will not shut down, because the kernel panics.
> That's the pass condition, not a regression. If you want the boot to *survive* the fault so the
> rest of the log still prints, that means handling the fault by killing the task — process
> lifetime, i.e. Phase 7. Resist it; the panic is more informative right now.

---

## 🧩 C3 — Prove it's actually unprivileged ⬜

**Goal:** three cheap experiments that each fail in a *different, specific* way. Together they
close off "it looked like it worked".

The dump in C2 proves the CPU *thinks* the code was unprivileged. These prove the protections
are actually enforced. Change the blob, one experiment at a time, and put it back afterwards.

| Experiment | Blob | Expected |
|---|---|---|
| **Read a kernel address** | load from any HHDM address, e.g. `mov rax, [0xffff800000000000]` / `ldr x0, [x1]` with a kernel VA | page fault (#PF / data abort), fault address = the kernel address. Proves `VMM_USER`'s absence on kernel pages is enforced, not decorative. |
| **Write to the code page** | store to `0x40000000` | your call: it *succeeds* today, because C0 mapped the code page `VMM_WRITABLE`. Map it read-only instead and it faults. A one-flag demonstration of W^X. |
| **Return with nothing on the stack** | `ret` as the first instruction | fault at address `0` — the "no `task_exit_guard` on a user stack" decision from A1, seen working. |

The first one is the important one. A privilege level that faults on `hlt` but happily reads
kernel memory would mean the page tables — not the CPU mode — are wrong, and that is a
distinction worth *observing* rather than assuming.

**Verify:** each experiment produces its own distinct dump, and putting the original blob back
reproduces C2 exactly.

---

## 🅰️ A2 — x86_64: SMEP ⬜ *(optional, any time after A1)*

**Goal:** make the *kernel* accidentally executing user memory an immediate fault instead of a
silent catastrophe.

Per [`PHASE4_PRIVILEGE.md`](PHASE4_PRIVILEGE.md) Chapter 4, SMEP is free and worth enabling now;
SMAP is not, because it requires `stac`/`clac` around every deliberate user access and there are
none until Phase 5's `copy_from_user`.

SMEP is CR4 bit 20. Set it in `arch_init()`, after checking it's supported (`CPUID.(EAX=7,ECX=0):EBX.SMEP`,
bit 7) — QEMU's default CPU model may not expose it, and setting an unsupported CR4 bit is a `#GP`
at boot, which is a memorably bad way to find out.

**Verify:** boot is unchanged (nothing in the kernel executes user pages, so nothing should
change). Then prove it works: deliberately call through a pointer to `USER_CODE_ADDR` from kernel
code and confirm you get a page fault *with the instruction-fetch bit set in the error code*
rather than the blob running in ring 0. Undo it immediately — like C3, a demonstration, not a
test.

---

## 🔬 Debugging tools worth knowing before you need them

Phase 3's list still applies (`-d int`, `-d int,cpu_reset`, `-D logs/…`, the QEMU monitor,
`info registers`, `nm | sort`). Three matter specifically here:

- **`-d int` shows the privilege transition itself.** On x86_64, QEMU prints `CPL=3` in its
  interrupt traces once the drop has happened. That answers "did it drop?" independently of your
  own dump, which is valuable precisely when you suspect your dump.
- **`info registers` in the monitor, while hung.** For the "didn't fault, hit the backstop loop"
  case, `CPL`/`CS` tells you immediately whether you're spinning at ring 3 (the drop worked, the
  instruction didn't fault — investigate the instruction) or ring 0 (the drop never happened).
- **The blob address is a landmark.** Every fault at `0x40000000`-something is *your* code. A
  fault anywhere else, especially in the HHDM range, is the kernel and means the problem is on
  the way *in*, not after arrival.

---

## 🕳️ Failure modes, collected

Grouped by symptom, because that's how you'll meet them.

**Triple fault / instant reset, no output at all.**
- x86_64, `tss.rsp0` wrong or unset: the CPU cannot build a trap frame when the fault arrives
  from ring 3, and a fault while handling a fault while handling a fault resets the machine.
  `arch_set_kernel_stack()` is called on every switch, so this means the user task's
  `kernel_stack_top` is bad — or the task was never created by `task_create_user()`.

**Plausible-looking garbage in kernel variables after the first trap from userland.**
- The user task is running on task 0's kernel stack (Limine's), so the trap frame landed on live
  C locals. See C1's callout. Not a triple fault, which is what makes it expensive.

**`#GP` immediately, `RIP` = the blob address, but `CS` is `0x08`.**
- The drop never happened: the frame's `cs` didn't have RPL 3, so `hlt` executed in ring 0 and
  faulted for an unrelated reason. Check `(frame->cs & 3) == 3` in A1's verify step.

**AArch64: fault at address `0` on the blob's first store.**
- `vector_type < 8`, so the exit path never wrote `SP_EL0`, so the blob is running on the
  poisoned stack pointer `arch_init()` installed. See B1.

**Page fault at the blob's address, error code says "not present".**
- The mapping isn't reaching the CPU. Check `vmm_virt_to_phys()` (C0), and remember the walk needs
  the user bit on *every* level — already handled by `get_or_create_table()`, but worth confirming
  the context you mapped into is the one that's active.

**Page fault at the blob's address, error code says "protection violation" / permission fault.**
- The page is mapped but not user-accessible: `VMM_USER` missing on the leaf.

**It runs and does NOT fault — clean hang in the backstop loop.**
- The privilege drop didn't happen and the instruction was legal in ring 0. `info registers` →
  `CPL`. This is why the loop is there.

**Nothing at all after `[IRQ  ] Enable IRQ`, no panic, no reset.**
- Interrupts are masked in the fabricated frame (`IF` clear / `I` set in `SPSR`), so the task got
  the CPU and never gave it back. Phase 3's most common failure, unchanged here.

**The dump prints on screen but the log file is truncated before it.**
- Not a kernel bug: something buffering sits between QEMU and `tee`. The pipeline is a plain
  `| tee` today precisely so this cannot happen — see the note at the top of this document.

---

## 📁 Files touched

- ⬜ `kernel/include/arch/arch.h` — `arch_task_init_user_frame()`
- ⬜ `kernel/arch/x86_64/idt.h` — user selector constants, `X86_64_STAR_USER_BASE`
- ⬜ `kernel/arch/x86_64/idt.c` — user code/data GDT entries, laid out to the `sysret` constraint
- ⬜ `kernel/arch/x86_64/arch.c` — `arch_task_init_user_frame()`; SMEP in CR4 (A2)
- ⬜ `kernel/arch/aarch64/arch.c` — `arch_task_init_user_frame()`, `SPSR`/vector-slot constants
- ⬜ `kernel/include/proc/task.h` — `task_create_user()`
- ⬜ `kernel/proc/task.c` — the above
- ⬜ `kernel/core/main.c` — the blob, the two user mappings, `task_create_user()` +
  `sched_add_task()` before `arch_irq_enable()`

Deliberately **not** touched: `isr.S`, `vectors.S` — entering userland is the *existing* exit path
restoring a frame it cannot tell is fabricated. If a change wants to reach into the trap stubs,
something upstream is wrong and that is the bug to fix. Also untouched: `sched.c` (the scheduler
does not need to know a task is unprivileged), `mm/`, `fs/`.

B1's constants join the ones Phase 3 left in `exceptions.h` — `AARCH64_SPSR_M_EL1H`, the four
DAIF masks, and `AARCH64_VECTOR_CURRENT_EL_IRQ`. Adding `M_EL0T` and `VECTOR_LOWER_EL_IRQ` beside
them puts the EL1-vs-EL0 and current-EL-vs-lower-EL pairs where they can be read against each
other, which is most of what makes B1 legible.

---

## 🪜 Verification

`core/test/` cannot reach any of this: a privilege transition is not assertable from inside a
single-threaded kernel self-test, which is what
[`PHASE4_PRIVILEGE.md`](PHASE4_PRIVILEGE.md)'s Verification chapter already says. Two pieces
*are* testable at rest, and are worth adding to `test_task.c`:

- **A fabricated user frame names an unprivileged level** — `(frame->cs & 3) == 3` on x86_64,
  `(frame->spsr & 0xF) == 0` on AArch64. Pure data, no execution.
- **A fabricated user frame still lies inside its own kernel stack** — `task_owns_frame()`, same
  invariant as Phase 3 C3, which must hold for user tasks too.

The rest is the boot log:

| Check | Expected |
|---|---|
| C0, both arches | boot unchanged; the mapping resolves and the blob reads back |
| C1, both arches | one extra task-dump line, still no privilege change |
| A1 / B1 frame inspection | `(cs & 3) == 3` / `(spsr & 0xF) == 0`, checked before entering |
| **C2, both arches** | **unhandled-exception dump at the blob's address, from an unprivileged frame** |
| C3 kernel-address read | page fault at the *kernel* address, not the blob's |
| A2 SMEP | kernel call into a user page faults on instruction fetch |

One thing worth carrying forward rather than doing here: **this user task can never stop.** It
faults, and the kernel panics — there is no `exit`, no reaping, and no way for a fault to kill
one task while the machine keeps running. Step 2 gives it a way to *talk* (`write`), which is the
phase's payoff; giving it a way to *die* is Phase 7. Until then, every user program ends in a
register dump, and that is fine.
