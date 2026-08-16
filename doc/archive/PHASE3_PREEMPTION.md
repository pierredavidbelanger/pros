# Working Document: Phase 3 — Preemption: Timers, Tasks & the Scheduler [STATUS: COMPLETE ✅]

> [!NOTE]
> **Where this ended.** All three Steps are **done**, and the payoff is real: three tasks — the
> boot task plus two kernel threads — take turns on the CPU driven only by the timer interrupt,
> their output interleaves, and the switch counters come out near-equal
> (`34 / 67 / 100` per task at the one-, two- and three-second marks on AArch64, against a
> predicted `300 / 3`). Nobody yields. **PrOS preempts.**
>
> - [`PHASE3_STEP1_TIMER.md`](PHASE3_STEP1_TIMER.md) — both architectures tick at 100 Hz
>   and shut down cleanly.
> - [`PHASE3_STEP2_KERNEL_STACKS.md`](PHASE3_STEP2_KERNEL_STACKS.md) — the trap path
>   builds its frame on the stack of whoever trapped, hands it to a scheduler on the way out,
>   and restores whichever frame the scheduler names.
> - [`PHASE3_STEP3_ROUND_ROBIN.md`](PHASE3_STEP3_ROUND_ROBIN.md) — a circular run queue,
>   `task_create()` with a fabricated initial frame per architecture, and a `sched_pick_next()`
>   that advances. The trap stubs needed no changes at all, which is the strongest evidence
>   Step 2 got its contract right.
>
> One thing is left open and is recorded in Step 3's document rather than fixed here:
> `SCHED_TIME_SLICE_TICKS` was never defined (the slice is one tick, but the named knob doesn't
> exist). The AArch64 `SPSR`/vector-slot constants shipped as literals and were named in
> `exceptions.h` shortly after the step closed. The `-mno-sse` build change in Chapter 7 also
> remains open, and belongs to Phase 7 where a second *user* task first exists.
>
> This document is the map: what the phase is actually made of, which pieces already existed in
> the tree, what each new concept *is* and why it exists, and an order of work where every Step
> still boots green. Where building a Step proved something here wrong, the text was corrected
> in place and the correction called out — the reasoning trail is worth more than a
> clean-looking plan.

> [!NOTE]
> **This document used to cover all of userland bring-up**, under the title "Preemption,
> Privilege Levels, Syscalls & `/bin/init`". That was eight Steps ending in one payoff, which
> is a long way to walk without a reward, so it was split along the capability boundaries
> Chapter 0's successor section had already identified:
>
> | | Capability | Payoff |
> |---|---|---|
> | **Phase 3 — this document** | interruption + multiplicity | two kernel threads interleave |
> | [**Phase 4 — Privilege**](PHASE4_PRIVILEGE.md) | distrust | ring-3 code calls `write` |
> | [**Phase 5 — Loading**](../PHASE5_LOADING.md) | loading | `/bin/init` runs from the filesystem |
>
> Chapter 0's inventory of what already exists still covers all three, because it's the
> starting line for all of them.

> [!NOTE]
> Mentor-mode reminder (see the note at the top of [`../README.md`](../../README.md)): this is a
> design reference to code from by hand, not code to paste in. Snippets are illustrative and
> deliberately incomplete.

---

## 🎓 Concept index — the genuinely new things

This phase introduces more new vocabulary than Phases 1 and 2 combined. This index exists so a
term can be pointed at and asked about. Each line is the one-sentence version; the numbered
Chapter expands it.

| Term | One-line version | Chapter |
|---|---|---|
| **Trap frame** | The complete CPU register state, saved on a stack, that a return-from-exception restores. | 1 |
| **Kernel stack (per-task)** | Every task needs its *own* kernel stack, because a task can be suspended mid-syscall. | 2 |
| **`rsp0` / `SP_EL1`** | The register the CPU uses to find the kernel stack when a user→kernel trap happens. | 2 |
| **IST** | x86 mechanism to force a *specific* stack for *specific* exceptions, regardless of what was running. | 2 |
| **Red zone** | 128 bytes below `rsp` that x86-64 leaf functions may use without adjusting `rsp`. | 2 |
| **Timer IRQ / preemption** | A periodic interrupt is the *only* reason a task can be taken off the CPU against its will. | 3 |
| **PIC / GIC** | The interrupt controller that decides which IRQs reach the CPU. Different on each arch. | 3 |
| **Context switch** | Swapping which task's register state is live — in this design, swapping which trap frame gets restored. | 4 |
| **PCB / `struct task`** | The kernel's record of a process: pid, state, stacks, address space, fds. | 4 |
| **Lazy FPU** | Only save/restore vector registers when a task that actually used them is switched. | 5 |

The rest of the userland vocabulary moved out with the phase split, and is indexed in the
document that now owns it: **Ring 3 / EL0**, **`iretq`/`eret`**, **`syscall`/`sysret`/`svc`**
and **`swapgs`** in [`PHASE4_PRIVILEGE.md`](PHASE4_PRIVILEGE.md); **`PT_LOAD`**,
**`p_filesz` vs `p_memsz`**, **auxv**, **`copy_from_user`** and **`-errno`** in
[`PHASE5_LOADING.md`](../PHASE5_LOADING.md).

---

## 🧭 Why this phase is shaped the way it is

Everything up to now runs in one context: `_start` calls subsystem initializers in order and
then shuts down (`core/main.c:66-67`). There is exactly one stack, one address space in use,
one thread of control, and no code the kernel doesn't trust.

Getting to userland breaks all four of those, and the four capabilities involved are genuinely
independent:

- **Interruption** — the CPU can stop what it's doing at an arbitrary instruction and resume
  it later, correctly.
- **Multiplicity** — more than one such interruptible thing exists, and the kernel picks
  which one runs.
- **Distrust** — some of those things run at a lower privilege level and their pointers,
  arguments, and behaviour must be treated as hostile.
- **Loading** — those things come from a file on disk rather than being compiled into the
  kernel.

**This phase takes the first two, and stops.** Not because the rest is harder, but because a
bug in a scheduler and a bug in a privilege transition look *identical* from the outside — the
machine resets — and debugging both at once is genuinely miserable. Distrust is
[Phase 4](PHASE4_PRIVILEGE.md); loading is [Phase 5](../PHASE5_LOADING.md).

The pleasant consequence: **everything in this phase happens in ring 0**, where a mistake
prints a register dump instead of triple-faulting. Preemption gets proven with *kernel* threads
before any lower privilege level exists to complicate it.

The order of work in Chapter 6 is built around getting the two capabilities one at a time.

---

## 🧱 Chapter 0: What already exists — Phase 3's actual starting line

More is already built than the roadmap's task list suggests. Worth knowing precisely, because
several Phase 3 "tasks" are really "finish and fix" rather than "write".

### Already there and usable as-is

- **A full trap frame on both architectures.** `struct x86_64_registers` (via `isr.S:52-89`)
  and `struct aarch64_registers` (`arch/aarch64/exceptions.h:6-14`) already save and restore
  the complete integer register state around a C handler. (**Both are now called
  `struct trap_frame`** — renamed in Step 2 Part C0 so generic code can hold a pointer to one
  without knowing what's in it. Only one arch is ever compiled, so there's no collision.) This is the single most important
  prerequisite for everything in this phase, and it exists. Notably the AArch64 frame already
  carries `elr` and `spsr` — which *are* the return address and processor state that `eret`
  consumes, i.e. the frame is already shaped to launch userland.
- **A TSS, loaded, with a valid GDT.** `idt.c:61-102` builds the GDT, builds `struct tss64`,
  and executes `ltr`. Roadmap task 1 is therefore partially done.
- **Per-address-space contexts.** `vmm_create_context()` / `vmm_switch_context()` /
  `vmm_destroy_context()` exist and work (`include/mm/vmm.h:45-61`), including the
  architecture-specific "how does a new root see kernel mappings" problem
  (`arch_vmm_new_context_root`). A process's address space is a solved problem.
- **`VMM_USER` already plumbed through to hardware** on both arches
  (`arch/x86_64/arch.c:39`, `arch/aarch64/arch.c:76-78`).
- **Demand paging with a per-context range** (`vmm_context.demand_page_lo/hi`,
  `mm/vmm.c:222-239`) — directly reusable for a growable user stack or heap.
- **A working filesystem to load `/bin/init` from**, with `sys_open`/`sys_read`.
- **Correct compiler flags in two places that matter.** `-mno-red-zone` on x86_64
  (`arch/x86_64/config.mk`) — see Chapter 2 for why that's load-bearing — and
  `-mgeneral-regs-only` on AArch64 (`arch/aarch64/config.mk`), which is why Chapter 5 is much
  easier on ARM than on x86.
- **FP/SIMD already enabled at EL0 on AArch64** — `CPACR_EL1.FPEN = 0b11`
  (`arch/aarch64/arch.c:11-13`), with the comment already anticipating this phase.

### Present but incomplete

- **`sys_tss.rsp0` is never set.** `tss_init()` sets `ist[0]` (`idt.c:67-68`) and nothing else.
  `rsp0` is *the* field the CPU reads on a ring-3→ring-0 trap. Left at zero, the very first
  syscall from userland pushes onto address 0 and triple-faults. This is a one-line-looking
  fix with a per-context-switch obligation behind it (Chapter 2).
- **The GDT has no user segments.** Only null / kernel code / kernel data / TSS
  (`idt.c:62-64`). Ring 3 needs a user code and user data descriptor, and `sysret` constrains
  *where in the GDT* they may sit ([`PHASE4_PRIVILEGE.md`](PHASE4_PRIVILEGE.md) Chapter 2).
- ~~**The IDT is only populated for vectors 0-31.**~~ **Fixed in Step 1.** `IDT_STUB_COUNT` is
  now 48, `isr.S` carries stubs for 32-47, and a `_Static_assert` ties that count to the PIC's
  vector range so the two can't silently disagree.
- **`struct file open_files[MAX_OPEN_FILES]` is a single kernel-global table**
  (`fs/vfs/file.c:7`). File descriptors are per-*process* in POSIX. This must move into the
  task structure ([`PHASE5_LOADING.md`](../PHASE5_LOADING.md) Chapter 2).
- **Syscalls return `-1`, not `-errno`** (all of `fs/vfs/file.c`), contradicting
  [`SYSCALL_DESIGN.md`](../SYSCALL_DESIGN.md) §4.

### Present and actively wrong once userland exists

These are not hypothetical — they are latent bugs that only fire when a lower exception level
starts trapping. Details and fixes in Chapter 2 and Chapter 4.

- **AArch64 vector stub saves the wrong stack pointer** — and this is *worse* than the original
  text below claimed. `vectors.S:21` does `mov x3, sp`, but taking an exception to EL1 sets
  `PSTATE.SP = 1`, so by the time that instruction runs `sp` is **SP_EL1**, whatever the
  interrupted context was using. Step 1 established that this kernel runs **EL1t** (Limine
  leaves `SPSel = 0`, nothing has changed it), so kernel code's stack pointer is SP_EL0 and
  SP_EL1 is never deliberately set at all. `regs->sp` therefore holds a leftover SP_EL1 value
  today, not the interrupted stack pointer — the `SP:` field in the panic dump
  (`exceptions.c:77`) is a lie, and has been since it was written. It happens to be harmless
  only because `vectors.S:125-126` writes that junk back to SP_EL1, which nothing reads.
  Once userland exists, the same instruction reads the kernel stack instead of the user's
  SP_EL0, which is the originally-documented bug arriving on top of this one.
- **AArch64 vector stub uses `tpidrro_el0` as a scratch register** (`vectors.S:12`).
  That register is userland's read-only thread pointer — the AArch64 TLS register. Clobbering
  it on every trap corrupts thread-local storage the moment libc exists.
- **A single, global exception stack per architecture.** `x86_64_exception_stack[16384]`
  (`idt.c:37`) and `exception_stack_bottom` (`vectors.S:3-4`). One stack shared by all
  traps means no task can be suspended inside a trap handler — which is exactly what
  preemption requires (Chapter 2).

### Where each of these gets fixed

None of them are standalone chores to do up front — every one is folded into a Step at the
point where it first becomes load-bearing. Most land in this phase; the last three belong to
the phases that split off.

| Existing problem | Fixed in |
|---|---|
| ✅ IDT only populated for vectors 0-31 | **Phase 3 Step 1** — needs gates ≥ 32 for the timer IRQ |
| ✅ `tss.rsp0` never set | **Phase 3 Step 2** — set via `arch_set_kernel_stack()`; see the caveat below |
| ✅ `vectors.S:21` saves SP_EL1 instead of the interrupted SP | **Phase 3 Step 2** |
| ✅ `vectors.S:12` clobbers `tpidrro_el0` | **Phase 3 Step 2** — the scratch trick is gone entirely |
| ✅ One global exception stack per arch | **Phase 3 Step 2** |
| ✅ `#PF` routed to IST1, which does not nest | **Phase 3 Step 2** |
| ✅ Kernel runs EL1t, so SP_EL0 is doing double duty | **Phase 3 Step 2** (new — found by Step 1) |
| No user segments in the GDT | [**Phase 4**](PHASE4_PRIVILEGE.md) Step 1, constrained by its Step 2 ⚠️ |
| `open_files[]` is kernel-global | [**Phase 5**](../PHASE5_LOADING.md) Step 2 |
| Syscalls return `-1`, not `-errno` | [**Phase 5**](../PHASE5_LOADING.md) Step 2 |

> [!NOTE]
> **`tss.rsp0` is set, but not yet to a *safe* value.** Task 0 runs on the stack Limine handed
> it, and it cannot be moved off a stack it is standing on, so `task_init_boot()` records the
> current `sp` as its `kernel_stack_top` and that is what reaches `tss.rsp0`. The CPU never
> reads it while nothing traps from ring 3, so this is dormant rather than broken — but the
> first return from EL0 would build a trap frame on top of task 0's live locals. Carried
> forward to [`PHASE4_PRIVILEGE.md`](PHASE4_PRIVILEGE.md) Chapter 5.

One consequence of that distribution is worth knowing before starting, because it isn't obvious
from the Step list.

**Step 2 is heavier than it reads.** Six of the ten land there, and four are the same
file. `vectors.S` gets substantially rewritten rather than patched: stop switching to
`exception_stack_top`, build the frame on the stack the trap arrived on, read the user stack
with `mrs x3, sp_el0` on a lower-EL entry, and drop the `tpidr_el1`/`tpidrro_el0` scratch
trick entirely. That trick only exists *because* the stub switches stacks before it has
anywhere to spill — remove the global stack and the need for it disappears with it. So the
four are one coherent change, not four. And none of them are even *possible* until the kernel
moves to EL1h, because "the stack the trap arrived on" is SP_EL1, which today holds nothing.

(The GDT item carries its own ordering trap — the descriptors must be laid out to `sysret`'s
constraint the first time even though nothing enforces it until Phase 4's second Step. It's
written up where it applies, in [`PHASE4_PRIVILEGE.md`](PHASE4_PRIVILEGE.md) Chapter 2.)

### None of these are live bugs today

Worth stating explicitly, since "actively wrong" could be misread as "currently broken". Each
of these is dormant until something traps from a lower exception level:

- ~~`mov x3, sp` is *correct* today — every trap currently comes from EL1, where `sp` is
  already the right value.~~ **This was wrong**, and Step 1's `SPSel` discovery is what
  exposed it: the kernel runs EL1t, so `sp` after exception entry is SP_EL1 and the
  interrupted stack pointer was SP_EL0. `regs->sp` is junk *today*. It is still not a live
  bug, because the only consumer is a diagnostic print in a path that ends in `kpanic`.
- `tpidrro_el0` matters only once thread-local storage exists, which arrives with the C library
  in Phase 9.
- The shared exception stack only corrupts if trap handlers nest or tasks are suspended
  inside one. The nearest live candidate — a nested `#PF` on the shared IST1 stack — was
  checked and isn't reachable in a way that matters: `try_handle_hhdm_mmio_fault`
  (`mm/vmm.c:206-219`) only touches page-table pages in usable RAM that Limine already
  mapped, and the one path that *could* nest is the failure `kprintf` at `mm/vmm.c:234`,
  which ends in `kpanic` regardless. The corruption would be downstream of an
  already-fatal error.

That's why they're listed as problems to fix *at a specific step* rather than as a cleanup
pass to run first.

**All three are now fixed, in Step 2.** The nested-trap case in the third bullet stopped being
hypothetical along the way: C4 deliberately took a data abort inside the timer IRQ handler on
AArch64 and watched it nest correctly, with the outer frame's `elr` intact across it. The
reasoning above — "not reachable in a way that matters" — was right about the *risk* and wrong
about the *difficulty*: staging the nesting deliberately turned out to be about fifteen lines.

---

## 🏗️ Chapter 1: The one idea that unifies this phase — the trap frame *is* the process

Almost everything in Phase 3 is easier once this clicks, so it goes first.

When an exception or interrupt happens, the CPU and then the stub in `isr.S` / `vectors.S`
push the entire register state onto a stack. When the handler returns, the stub pops it back
and executes `iretq` / `eret`, and the CPU resumes as if nothing happened.

Note what that means: **a suspended thread of execution is entirely described by a trap frame
plus a stack.** Not "mostly described" — entirely. Every register, the program counter
(`rip` / `elr`), the flags (`rflags` / `spsr`), and the stack pointer are all in there.

Three seemingly unrelated Phase 3 features are then the same operation wearing different hats:

| Feature | What it actually is |
|---|---|
| Returning from a page fault | Restore the frame you saved. |
| **Switching tasks** | Restore *a different* frame than the one you saved. |
| **Entering userland the first time** | Restore a frame you *fabricated*, with `cs`/`spsr` set to user mode. |

There is no separate "jump to usermode" mechanism and no separate "switch task" mechanism
needed. There is one mechanism — return-from-trap — used three ways.

This is why the recommended design in Chapter 4 makes the C handler *return a frame pointer*
instead of `void`, and the stub restores from whatever it gets back. Roughly four lines of
assembly per architecture buys the entire context switch.

```
                 ┌─────────────────────────────────────────┐
   trap ────────►│ stub pushes registers onto kernel stack  │
                 └───────────────────┬─────────────────────┘
                                     ▼
                      C handler gets struct *regs
                                     │
              ┌──────────────────────┼──────────────────────┐
              ▼                      ▼                      ▼
      returns same frame     returns another task's   returns a fabricated
      (normal resume)        frame (context switch)   frame (enter userland)
              └──────────────────────┼──────────────────────┘
                                     ▼
                 ┌─────────────────────────────────────────┐
                 │ stub pops from returned frame, iretq/eret│
                 └─────────────────────────────────────────┘
```

Worth sitting with before writing anything: *why* does `iretq` restoring a frame whose `cs`
has RPL 3 constitute "entering userland", when it's the same instruction used to return from
a divide-by-zero? (Answer in [`PHASE4_PRIVILEGE.md`](PHASE4_PRIVILEGE.md) Chapter 1.)

---

## 🏗️ Chapter 2: Kernel stacks — the piece that is quietly hard

> [!NOTE]
> This chapter and Chapter 4's "surgical version" are what **Step 2** builds. The Parts, the
> ordering, and the verification for each are in
> [`PHASE3_STEP2_KERNEL_STACKS.md`](PHASE3_STEP2_KERNEL_STACKS.md).

### Why one global exception stack stops working

Today's model: a trap switches to a fixed stack (`idt.c:37` via IST, `vectors.S:20-22`
explicitly), runs the handler to completion, and returns. It works because handlers never
block and never get interrupted.

Preemption breaks that. Once a timer interrupt can fire *while task A is inside a syscall*,
task A's half-finished kernel state — its C local variables, its return addresses, its saved
trap frame — is sitting on that stack, and task B is about to trap and overwrite it.

So: **one kernel stack per task**, allocated when the task is created, and the CPU must be
told where it is *before* the task's first trap.

Sizing: 16 KiB (4 pages) is the conventional starting point and matches the exception stacks
already in the tree. It has to hold the trap frame plus the deepest kernel call chain — and
`vmm_free_table_recursive` (`mm/vmm.c:76`) recurses, so that chain isn't trivially bounded.
Worth knowing that Linux ran on 8 KiB for x86-64 for years and moved to 16 KiB.

A recurring recommendation: put a **guard value** at the low end of each kernel stack and
check it on every context switch. Kernel stack overflow does not produce a clean fault (it
just walks into whatever the heap put below it) and the resulting corruption is
indistinguishable from a random pointer bug three subsystems away.

### How the CPU finds it — the arch difference

This is the first place the two architectures genuinely diverge, and it's worth understanding
*why* they diverge rather than memorising both.

**x86_64** has one stack pointer register, `rsp`, shared by all privilege levels. So on a
ring-3→ring-0 transition the CPU must get the kernel stack from *somewhere else*: it reads
`tss.rsp0` from the TSS that `ltr` loaded. That's the entire purpose of the TSS in 64-bit
mode — it is not a task-switching mechanism any more, it's a table of stack pointers.

```c
// on every context switch, before returning to the new task
sys_tss.rsp0 = (uint64_t)new_task->kernel_stack_top;
```

Forget this line and the symptom is a triple fault on the first syscall of the *second* task,
which looks nothing like a scheduler bug.

**AArch64** has *two* stack pointer registers, `SP_EL0` and `SP_EL1`, and the exception level
selects which one is live. The kernel at EL1 (with `SPSel = 1`) uses SP_EL1; userland at EL0
uses SP_EL0. A trap from EL0 to EL1 therefore switches stacks *automatically*, with no table
lookup — the hardware already holds both pointers. The equivalent obligation is just:

```
// on context switch
msr sp_el1, <new kernel stack top>   // or keep SP_EL1 as the live sp and switch it directly
```

The consequence for `vectors.S`: **the whole `exception_stack_top` dance must go.** On entry
from EL0, `sp` is *already* the correct per-task kernel stack. Switching away from it to a
global stack is exactly the wrong move. The stub becomes "push the frame onto the stack I'm
already on", which also removes the need for the `tpidr_el1`/`tpidrro_el0` scratch trick — the
frame can be built directly with `stp` at negative offsets from `sp`.

### IST: keep it, but for the right exception

`idt.c:122-124` currently routes **both** #DF (8) and #PF (14) to IST1.

IST forces a stack switch *unconditionally* — including for a fault that happened in the
kernel, on a perfectly good stack. That's exactly right for #DF: a double fault often means
the stack itself is broken, so a known-good stack is the only way to report anything.

It's wrong for #PF once demand paging and userland are live, for two reasons:
- Page faults are *routine* now (that's what demand paging is), so they should run on the
  faulting task's own kernel stack like any other trap.
- IST does not nest. A #PF taken while already on IST1 re-enters at the same stack top and
  silently overwrites the outer frame. Today nothing nests; with preemption and user memory
  access, nesting stops being exotic.

Recommendation: **#DF keeps IST1, #PF moves to the normal path** (`ist = 0`) at the same time
per-task kernel stacks land. Optionally give NMI its own IST slot later.

### The red zone — why `-mno-red-zone` is already in `config.mk`

The x86-64 System V ABI lets a leaf function use the 128 bytes *below* `rsp` without
adjusting `rsp`. Hardware knows nothing about this. An interrupt arriving mid-function pushes
its frame right over those 128 bytes.

`arch/x86_64/config.mk` already passes `-mno-red-zone`, so kernel code never relies on it —
this is already correct and needs no action. It's listed here because it is the classic
"kernel works until you enable interrupts, then corrupts randomly" bug, and knowing it's
already handled is worth more than rediscovering it.

Userland code *does* use the red zone, which is fine: the kernel never runs on the user stack.

---

## 🏗️ Chapter 3: Timers and interrupts — where the two arches diverge most

> [!NOTE]
> **Built, in Step 1.** What follows is the design as planned; the corrections below are what
> actually happened. The full account is in [`PHASE3_STEP1_TIMER.md`](PHASE3_STEP1_TIMER.md).
>
> - **x86_64 needed a third thing this section didn't mention: the LAPIC.** A correctly
>   programmed PIC and PIT deliver nothing, because the LAPIC's LINT0 LVT entry resets to
>   masked. `arch/x86_64/lapic.c` puts it in virtual wire mode. "Two masks" was wrong; there
>   are three.
> - **The AArch64 IRQ vector slot is 1, not 5** (see the corrected claim below). This cost a
>   debugging session.
> - **Neither architecture gets a DTB from Limine.** AAVMF consumes QEMU's FDT and publishes
>   ACPI instead, so `dtb_request.response` is NULL on *both*. The GIC bases are hard-coded
>   with a comment, and the ACPI walker that would replace them is in
>   [`SIDEQUEST.md`](../SIDEQUEST.md).
> - **`arch_timer_ticks()` does not exist.** The counter lives entirely in generic code
>   (`core/timer.c`), which is the stricter version of what this section argues for.

Preemption requires a periodic interrupt. Without it a task that never syscalls owns the CPU
forever. This is the single most arch-specific piece of Phase 3, and **AArch64 is
significantly more work than x86_64 here** — worth knowing before starting, because it's the
one step where "do both arches at once" is a bad idea.

### x86_64

Two viable routes:

- **8259 PIC + 8254 PIT.** Remap the PIC (it defaults to vectors 8-15, colliding with CPU
  exceptions), program the PIT divisor for the desired frequency, unmask IRQ 0, add an IDT
  gate at vector 32. Around 60 lines, no calibration, works everywhere.
- **LAPIC timer + IOAPIC.** The modern path, needed for SMP later, but requires finding the
  LAPIC base, calibrating the timer frequency against another time source, and masking the
  legacy PIC anyway.

Recommendation: **PIT first.** It gets preemption working in one sitting and is trivially
deleted later. The LAPIC is a Phase 5+ concern, arriving with SMP, and doing it now buys
nothing that isn't also a distraction.

Either way, `idt_init()`'s loop stops at 31 (`idt.c:121`) — hardware IRQ gates need adding
above that, and every interrupt gate needs an EOI (end-of-interrupt) write on the way out or
the controller never sends a second one.

### AArch64

There is no PIT. The Generic Timer is in the CPU (`CNTFRQ_EL0` gives its frequency,
`CNTV_TVAL_EL0` sets a countdown, `CNTV_CTL_EL0` enables it), which is the easy half. The
hard half is that its interrupt is a **PPI** routed through the **GIC** — on QEMU `virt` with
`-cpu cortex-a72`, that's a GICv2, and it must be initialised: distributor enable, CPU
interface enable, priority mask, enable interrupt 27 (the EL1 virtual timer PPI).

The GIC also has to be *found*. Its MMIO base is in the device tree that QEMU passes, and
Limine's DTB request is not currently used anywhere in `core/boot.c`. Two options: parse the
DTB (correct, reusable in Phase 7 for VirtIO), or hard-code QEMU `virt`'s known base
(`0x08000000` distributor / `0x08010000` CPU interface) with a loud comment. Hard-coding
first and parsing later is defensible; hard-coding it *silently* is not.

Also required: `daifclr` to actually unmask IRQs at EL1, and an IRQ dispatch branch ahead of
the unhandled-exception panic in `exceptions.c`.

> [!IMPORTANT]
> **Correction.** This originally said the current-EL IRQ slot is `VECTOR_ENTRY 5`. It is
> **1**. The vector table is split by *which stack pointer the interrupted context was using*,
> not only by exception kind: `SPSel = 0` (EL1t) vectors to slots 0-3, `SPSel = 1` (EL1h) to
> slots 4-7. Limine hands the kernel `SPSel = 0`. `exceptions.c` therefore branches on
> `vector_type == 1 || vector_type == 5`, and Step 2 is where the kernel moves to EL1h and 5
> becomes the true answer. Slot 9 for lower-EL was right.

### One shared abstraction

Both sides should end up behind something like:

```c
// include/arch/arch.h — as built in Step 1
void arch_timer_init(uint32_t hz);
void arch_irq_enable(void);
void arch_irq_disable(void);
```

with the tick counter in generic code, feeding `sys_clock_gettime` and `sys_nanosleep` later.
Keeping the *policy* (how many ticks per slice, when to reschedule) out of `arch/` matters
more than the exact function list. The draft above also listed an `arch_timer_ticks()`; it was
dropped, because "how many times has it fired" is not an architectural question —
`core/timer.c` owns the counter outright and both IRQ handlers just call `timer_tick()`.

---

## 🏗️ Chapter 4: The scheduler

### `struct task`

Start deliberately small. Every field added now is a field that has to be correct in `fork`.

```c
struct task {
    uint64_t pid;
    int      state;              // RUNNING / READY / BLOCKED / ZOMBIE
    void    *kernel_stack;       // base, for freeing
    void    *kernel_stack_top;   // what rsp0 / SP_EL1 gets set to
    struct trap_frame *frame;    // saved frame, points into kernel_stack when suspended
    struct vmm_context *ctx;     // address space (already exists!)
    struct file *fds[MAX_OPEN_FILES];  // moved out of fs/vfs/file.c
    struct task *next;           // run queue
    int      exit_code;
};
```

`struct trap_frame` should be a *typedef-free* per-arch alias so generic scheduler code can
hold a pointer without knowing the layout — the two existing structs
(`x86_64_registers` / `aarch64_registers`) are exactly it, they just need a common name in
`include/arch/arch.h`.

Deliberately absent for now: priorities, per-CPU run queues, credentials, cwd, signal state,
timers. Each earns its way in when something needs it.

### The switch itself — the surgical version

The conventional design has a `switch_to()` in assembly that saves callee-saved registers on
one kernel stack and restores them from another. It's what Linux does, and it's necessary for
*voluntary* switches (a task blocking inside a syscall).

But there's a much smaller first step available, given the trap frame that already exists:
**if the only reschedule point is the timer interrupt, no new assembly is needed at all** —
only a change of the stub's contract.

```
current (both arches):     handler is void, stub restores from the frame it built
proposed:                  handler returns struct trap_frame *, stub restores from THAT
```

x86_64 (`isr.S:69-71`): `call x86_64_exception_handler` already puts the frame pointer in
`%rdi`; the return value lands in `%rax`. Adding `movq %rax, %rsp` before the pop sequence is
the entire change.

AArch64 (`vectors.S:91-92`): `mov x0, sp` / `bl aarch64_exception_handler`, return value in
`x0`. Adding `mov sp, x0` after the call is the entire change.

Then the scheduler is ordinary C:

```c
struct trap_frame *schedule(struct trap_frame *current_frame) {
    task_current->frame = current_frame;
    task_current = pick_next();
    // arch obligations before returning to a different task
    arch_set_kernel_stack(task_current->kernel_stack_top);   // tss.rsp0 / SP_EL1
    if (task_current->ctx != vmm_current_context) {
        vmm_switch_context(task_current->ctx);
    }
    return task_current->frame;
}
```

Two things this buys beyond brevity. First, "start a brand new task" needs no special code —
its `frame` is just a fabricated one ([`PHASE4_PRIVILEGE.md`](PHASE4_PRIVILEGE.md) Chapter 1). Second, it forces the ordering that's easy to
get wrong: **switch the address space before returning to the frame, but note that the kernel
stack you're currently standing on must stay mapped across that switch.** It does, because
kernel mappings are shared by every context (`arch_vmm_new_context_root`) — but that's a real
invariant worth writing down rather than a happy accident.

The limitation, stated honestly: this handles *preemptive* switches only. A task that blocks
inside a syscall (waiting on a pipe, on a child, on input) needs the voluntary path, and that
does need a real `switch_to`. That's why Chapter 6 puts blocking after `wait4` rather than
pretending it's free.

### Where reschedule is allowed to happen

Cleanest rule for a first implementation: **reschedule only on the way out of a trap, and only
when returning to a point that is safe to leave.** Set a `need_resched` flag in the timer
handler, and act on it at a single place near the end of the trap path. Rescheduling from
arbitrary depths inside kernel code is what makes a kernel "preemptible", and it requires
locking discipline this kernel doesn't have yet. Non-preemptible-kernel first is the correct
scope.

---

## 🏗️ Chapter 5: FPU / SIMD state

### The two architectures are in very different positions

**AArch64 is nearly free.** `-mgeneral-regs-only` (`arch/aarch64/config.mk`) means the kernel
never touches V registers — a quick check confirms the built kernel contains zero FP/SIMD
instructions. So user FP state can't be clobbered by kernel code at all, and saving it is only
needed when switching between two *user* tasks. FP/SIMD is already enabled at EL0
(`CPACR_EL1.FPEN`, `arch/aarch64/arch.c:11-13`).

**x86_64 is not.** The kernel build sets no `-mno-sse`, and the built kernel contains ~205
instructions using `%xmm` registers (`objdump -d kernel/bin/kernel-x86_64 | grep -c '%xmm'`) —
the compiler uses SSE for memory copies and struct moves, and the vendored `printf` uses it
for float formatting. So on x86_64 the kernel *will* destroy user vector state on any trap
unless something prevents it.

Two ways to resolve that, and the choice is worth making explicitly:

- **Add `-mno-sse -mno-sse2 -mno-mmx -mno-80387` to the x86_64 build**, matching what AArch64
  already does. Then the situation becomes symmetric and lazy FPU switching works. Cost: the
  compiler falls back to slower byte-wise copies, and any float in kernel code stops compiling
  (there shouldn't be any — the `printf` float support can be compiled out).
- **Save/restore eagerly at every user boundary.** Correct, and expensive: `fxsave` is 512
  bytes, `xsave` with AVX-512 is over 2 KiB, on every single trap including page faults.

Recommendation: **make the x86_64 kernel SSE-free, mirroring AArch64.** It makes both
architectures obey the same rule ("the kernel does not use vector registers"), which in turn
makes the lazy scheme below actually correct rather than approximately correct.

### Lazy switching

Once the kernel is vector-free, state only needs to move when switching between two user tasks
that both use FP. The standard trick: disable FP access on switch (x86: set `CR0.TS`;
AArch64: clear `CPACR_EL1.FPEN`), and let the resulting trap (#NM / EC=0x07) tell you a task
actually wants it. Save the previous owner's state, restore this task's, re-enable, resume.

A task that never touches a float never pays. Worth deferring the whole thing until a second
user task exists — with one process there is nothing to corrupt, and the fixed cost of doing
it wrong is measured in very confusing floating-point results much later.

---

## 🪜 Chapter 6: The Steps

Same discipline as Phase 2: every Step boots green on its own, and each one is independently
verifiable. All three happen in ring 0.

**✅ Step 1 — Timer and interrupts, no scheduler.** → [`PHASE3_STEP1_TIMER.md`](PHASE3_STEP1_TIMER.md)
`arch_timer_init`, IDT gates above 31 + PIT + PIC remap + LAPIC virtual wire on x86_64,
GICv2 + Generic Timer on AArch64. Acceptance met: both architectures tick at 100 Hz, print
once per second, and reach `arch_shutdown()` on their own. Broken into ten individually
verifiable Parts in its own working document.

**✅ Step 2 — `struct task`, per-task kernel stacks, and the stub contract change.**
→ [`PHASE3_STEP2_KERNEL_STACKS.md`](PHASE3_STEP2_KERNEL_STACKS.md)
The four-line stub change from Chapter 4 (handler returns a frame), `tss.rsp0` / `SP_EL1`
maintenance, the move to EL1h on AArch64, and the `vectors.S` correctness fixes from Chapter 0.
Still one task, so the "scheduler" always returns the frame it was given. Acceptance met:
nothing observable changed — plus one task-dump line and five new `[KSTK]` self-tests. Broken
into eight individually verifiable Parts in its own working document, whose C4 is worth reading
before Step 3: it's the run that proved the frames really are per-stack rather than assumed to
be.

**✅ Step 3 — Two kernel threads, round-robin.**
→ [`PHASE3_STEP3_ROUND_ROBIN.md`](PHASE3_STEP3_ROUND_ROBIN.md)
Second and third tasks with fabricated frames whose `rip`/`elr` point at a kernel function, all
printing. Acceptance met: **interleaved output**, sustained for the full three seconds, with the
boot task's countdown still reaching 3 and near-equal switch counters proving it's round-robin
rather than erratic. This is the milestone where preemption is real, it is **Phase 3's stated
goal**, and it happened entirely in ring 0 where a mistake printed a register dump instead of
resetting the machine.

Step 2 left it a narrower job than it looked. `sched_on_trap_exit()` already ran on every trap,
already stored the outgoing frame in `current->frame`, already checked the incoming task's stack
guard, and already called `arch_set_kernel_stack()` before returning a different frame — all of
it exercised with `sched_pick_next()` returning `current`. What was missing was a run queue, a
`task_create()` that allocates a kernel stack and fabricates an initial frame, and a
`sched_pick_next()` that advances. The switch mechanism itself was built and running.

The one genuinely new idea turned out to be the **bootstrap asymmetry**: a task that has run
before has a real frame the stub wrote, but a brand-new task has never trapped, so the frame it
resumes from has to be forged by hand — `arch_task_init_frame()`, the single new `arch_*` hook
in the step. Broken into six individually verifiable Parts in its own working document, whose
retrospective is worth reading for the two failures that cost the most time: a missing `return`
that left `task->frame` NULL (which reads exactly like a scheduler bug and isn't one), and a
panic that appeared to be a hang because `ansifilter` buffered it away.

Everything that used to follow moved out with the phase split: ring 3 and the syscall entry
path are [`PHASE4_PRIVILEGE.md`](PHASE4_PRIVILEGE.md); the ELF loader and the real syscall
surface are [`PHASE5_LOADING.md`](../PHASE5_LOADING.md); `fork`/`execve`/`wait4` and the real
`switch_to` are Phase 7, which will earn its own document once Steps here have taught what
`struct task` actually needs to hold.

**FPU (Chapter 5) slots in wherever a second *user* task first exists** — Phase 7. The
`-mno-sse` build change can land any time and is worth doing early since it's a one-line
change with a compile-time-visible blast radius.

---

## ⚖️ Chapter 7: Design decisions to make deliberately

Recommendations given, but these are the calls worth making consciously rather than by
default. Several of these are the "open decisions" in `SYSCALL_DESIGN.md` §13.

| Decision | Options | Recommendation | Settled? |
|---|---|---|---|
| x86 interrupt controller | PIC+PIT vs LAPIC+IOAPIC | PIC+PIT — replaceable, unblocks Step 1 today | ✅ PIC+PIT, *plus* the LAPIC in virtual wire mode, which turned out not to be optional |
| AArch64 GIC base | DTB parse vs hard-code QEMU virt | Hard-code with a loud comment; discover it later | ✅ hard-coded; **DTB is not even available** (see [`SIDEQUEST.md`](../SIDEQUEST.md)), so the replacement will be ACPI, not FDT |
| AArch64 stack pointer selection | stay EL1t vs move to EL1h | EL1h — SP_EL0 must belong to EL0 | ✅ EL1h, in Step 2 Part B1 (question only surfaced during Step 1) |
| Kernel preemptibility | preemptible vs not | Not preemptible — reschedule only on trap exit | ✅ not preemptible; `need_resched` is set in the timer path and acted on only in `sched_on_trap_exit()` |
| Kernel vector registers | `-mno-sse` vs eager save | `-mno-sse`, matching AArch64's existing stance | ⬜ any time |
| Context switch mechanism | frame-swap vs `switch_to` | Frame-swap first; add `switch_to` in Phase 7 | ✅ frame-swap, built in Step 2 and proven by C1's throwaway frame copy |
| `#PF` stack | keep IST1 vs per-task stack | Per-task; IST1 stays for `#DF` only | ✅ Step 2 Part A1 |

The decisions that moved out with their chapters are recorded where they now apply:
`syscall`/`sysret` and `swapgs` in [`PHASE4_PRIVILEGE.md`](PHASE4_PRIVILEGE.md) Chapter 4; the
ELF segment copy and user-pointer validation in [`PHASE5_LOADING.md`](../PHASE5_LOADING.md)
Chapter 4.

---

## 🕳️ Chapter 8: Traps worth knowing about in advance

Collected because each one has a symptom that points somewhere other than the cause.

- **Missing EOI** → exactly one timer interrupt fires, ever. Preemption "works once". The
  mirror image — an interrupt *acknowledged but never handled* — storms instead: Step 1 hit
  915,230 IRQs in 13 seconds with no output at all, because the handler didn't recognise the
  vector slot.
- **Kernel stack overflow** → silent heap corruption. Guard values make it a clean panic.
- **Reusing one exception stack across tasks** → corruption proportional to how *well* the
  scheduler is working, which is a memorably bad debugging experience.
- **`tss.rsp0` unset** → triple fault on the first user→kernel trap, with no output at all.
  Set in Step 2; it costs nothing here and becomes fatal in Phase 4.
- **`tpidrro_el0` clobbered by the AArch64 stub** (`vectors.S:12`) → TLS corruption that only
  appears once a libc exists in Phase 9, long after the stub was last touched. Fixed in Step 2
  because that's when the stub is open anyway.

The traps that only fire once a lower privilege level exists — `sysret`'s GDT constraint,
`IA32_FMASK`, the fabricated `rflags` — are in [`PHASE4_PRIVILEGE.md`](PHASE4_PRIVILEGE.md)
Chapter 5. The loader's are in [`PHASE5_LOADING.md`](../PHASE5_LOADING.md) Chapter 5.

---

## 📁 Critical files

Existing, modified:

- ✅ `kernel/arch/x86_64/idt.c` — IDT gates above 31 (Step 1); `rsp0`, `#PF` off IST (Step 2);
  `task_dump_all()` from the panic path (Step 3)
- ✅ `kernel/arch/x86_64/isr.S` — stubs 32-47 (Step 1); handler returns a frame, restore from it
  (Step 2). Untouched by Step 3, as designed.
- ✅ `kernel/arch/aarch64/vectors.S` — per-task stack instead of the global one, correct
  `SP_EL0` save, `tpidrro_el0` scratch use dropped, handler returns a frame (Step 2). Untouched
  by Step 3, as designed.
- ✅ `kernel/arch/aarch64/exceptions.c` — IRQ dispatch before the panic path (Step 1);
  `task_dump_all()` from the panic path (Step 3)
- ✅ `kernel/include/arch/arch.h` — timer/IRQ primitives (Step 1); kernel-stack primitive and
  common `trap_frame` name (Step 2); `arch_task_init_frame()` (Step 3)
- ✅ `kernel/core/main.c` — timer init/enable/wait (Step 1); scheduler start (Step 2); two
  kernel threads plus `sched_add_task()` before `arch_irq_enable()` (Step 3)
- ⬜ `kernel/arch/x86_64/config.mk` — `-mno-sse` and friends. **Still open**, and it belongs to
  Phase 7 where a second *user* task first exists.
- ✅ `kernel/arch/aarch64/arch.c` — `msr spsel, #1` to move the kernel to EL1h (Step 2);
  `arch_task_init_frame()` (Step 3, though the design put it in `exceptions.c`)

New:

- ✅ `kernel/arch/x86_64/pic.c`, `io.h`, `lapic.c` — the last of these wasn't in the plan
- ~~`kernel/arch/x86_64/pit.c`~~ — the PIT is ~10 lines and lives in `arch.c`; it never earned
  a file
- ✅ `kernel/arch/aarch64/gic.c`, `timer.c`
- ✅ `kernel/core/timer.c` — the generic tick counter
- ✅ `kernel/proc/task.c`, `sched.c`, `kstack.c` — `kernel/Makefile` now globs
  `core mm drivers fs proc`. `kstack.c` wasn't in this plan at all; it came out of Step 2 and
  grew the stack-geometry API (`kstack_get_top`, `kstack_contains`, `kstack_get_usage`) in
  Step 3.
- ✅ `kernel/core/test/test_kstack.c` (Step 2), `test_task.c` (Step 3) — 11 checks between them

The files owned by the phases that split off are listed in their own documents:
[`PHASE4_PRIVILEGE.md`](PHASE4_PRIVILEGE.md) (user GDT entries, the MSRs, `syscall_entry.S`,
the dispatch table) and [`PHASE5_LOADING.md`](../PHASE5_LOADING.md) (`elf.c`, the fd table,
`errno.h`, `initrd/bin/init.c`).

## 📝 Docs to keep in step

- ✅ `ROADMAP.md` — Phases 3-12 restructured around payoff-per-phase. Also carries the naming
  conventions and the `doc/` vs `doc/archive/` split. Phase 3's list was rewritten from Steps to
  **What shipped** when the phase closed, per its own lifecycle table.
- ✅ `README.md` — roadmap section explains the archive convention. The subsystem table gained a
  row for Step 1's interrupt controllers and timer, and a **Tasks & scheduler** row when Step 3
  landed.
- ⬜ `SYSCALL_DESIGN.md` — §13's open decisions answered from Chapter 7 as they're settled.
  **Still open**; Phase 4 inherits it.

## 🪜 Verification

The `core/test/` suite can't reach most of this — a scheduler isn't assertable from inside a
single-threaded kernel self-test. The honest split:

**Still unit-testable** (added to `core/test/`): the kernel-stack guard-value check, and kernel
stack allocation/free returning `pmm_get_free_page_count()` to where it started — five `[KSTK ]`
checks from [`PHASE3_STEP2_KERNEL_STACKS.md`](PHASE3_STEP2_KERNEL_STACKS.md). Step 3
added six more `[TASK ]` checks: task create/destroy leaks nothing, a fresh task's fabricated
frame lies inside its own kernel stack, and `task_stack_intact()` reports a task with no known
stack extent as intact — that last one pins the C0 bug shut so it cannot come back.

**Only observable by booting**: preemption itself — the interleaved output from Step 3. That
needed a *stated expected output*, which is what the acceptance criteria in Chapter 6 were for.
It's also why `switch_count` exists: interleaved output alone is consistent with a scheduler
that switches erratically, and three near-equal counters are not.

The boot-time task dump was worth adding early exactly as predicted, and grew past pid/state/
stack usage into `task_dump_all()` from both architectures' panic paths. Most of the bad days
ahead are "which task was actually running when this happened", and that question now answers
itself on every crash.
