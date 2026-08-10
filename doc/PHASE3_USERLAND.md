# Working Document: Phase 3 — Preemption, Privilege Levels, Syscalls & `/bin/init` [STATUS: NOT STARTED ⬜]

> [!NOTE]
> **Where this stands.** Nothing written yet. This document is the map: what the phase is
> actually made of, which pieces already exist in the tree, what each new concept *is* and why
> it exists, and an order of work where every step still boots green.
>
> Phase 3 is the biggest jump in the roadmap so far. Phases 1 and 2 built things the kernel
> talks to. Phase 3 builds the thing that talks *to the kernel* — and that means the kernel
> stops being a single-threaded program that runs `_start` to completion.

> [!NOTE]
> Mentor-mode reminder (see the note at the top of [`../README.md`](../README.md)): this is a
> design reference to code from by hand, not code to paste in. Snippets are illustrative and
> deliberately incomplete.

---

## 🎓 Concept index — the genuinely new things

Phase 3 introduces more new vocabulary than Phases 1 and 2 combined. This index exists so a
term can be pointed at and asked about. Each line is the one-sentence version; the numbered
Part expands it.

| Term | One-line version | Part |
|---|---|---|
| **Trap frame** | The complete CPU register state, saved on a stack, that a return-from-exception restores. | 1 |
| **Kernel stack (per-task)** | Every task needs its *own* kernel stack, because a task can be suspended mid-syscall. | 2 |
| **`rsp0` / `SP_EL1`** | The register the CPU uses to find the kernel stack when a user→kernel trap happens. | 2 |
| **IST** | x86 mechanism to force a *specific* stack for *specific* exceptions, regardless of what was running. | 2 |
| **Ring 3 / EL0** | The unprivileged CPU mode userland runs in; privileged instructions fault there. | 3 |
| **`iretq` / `eret`** | The instruction that drops to userland — the *same* one that returns from an exception. | 3 |
| **`syscall`/`sysret`, `svc`** | The fast, dedicated user→kernel entry that isn't an exception. | 6 |
| **Red zone** | 128 bytes below `rsp` that x86-64 leaf functions may use without adjusting `rsp`. | 2 |
| **`swapgs` / `TPIDR_EL1`** | How the trap handler finds the kernel's own data when it has *no* usable register or stack. | 6 |
| **Timer IRQ / preemption** | A periodic interrupt is the *only* reason a task can be taken off the CPU against its will. | 4 |
| **PIC / GIC** | The interrupt controller that decides which IRQs reach the CPU. Different on each arch. | 4 |
| **Context switch** | Swapping which task's register state is live — in this design, swapping which trap frame gets restored. | 5 |
| **PCB / `struct task`** | The kernel's record of a process: pid, state, stacks, address space, fds. | 5 |
| **ELF `PT_LOAD`** | The only ELF program-header type a loader must understand to run a static binary. | 7 |
| **`p_filesz` vs `p_memsz`** | The difference between the two is `.bss`, and the loader must zero it. | 7 |
| **auxv** | The key/value array on the initial user stack that libc reads before `main`. | 7 |
| **`copy_from_user`** | Never trust a pointer that came from userland; validate before dereferencing. | 8 |
| **`-errno`** | The Linux ABI's error convention: negative return *is* the error code. | 8 |
| **Lazy FPU** | Only save/restore vector registers when a task that actually used them is switched. | 9 |

---

## 🧭 Why this phase is shaped the way it is

Everything up to now runs in one context: `_start` calls subsystem initializers in order and
then shuts down (`core/main.c:66-67`). There is exactly one stack, one address space in use,
one thread of control, and no code the kernel doesn't trust.

Phase 3 breaks all four of those at once, which is why it can't be done as one commit. The
four independent capabilities being added are:

1. **Interruption** — the CPU can stop what it's doing at an arbitrary instruction and resume
   it later, correctly.
2. **Multiplicity** — more than one such interruptible thing exists, and the kernel picks
   which one runs.
3. **Distrust** — some of those things run at a lower privilege level and their pointers,
   arguments, and behaviour must be treated as hostile.
4. **Loading** — those things come from a file on disk rather than being compiled into the
   kernel.

The order of work in Part 10 is built around getting these one at a time, because a bug in a
scheduler and a bug in a privilege transition look *identical* from the outside (the machine
resets), and debugging both at once is genuinely miserable.

---

## 🧱 Part 0: What already exists — Phase 3's actual starting line

More is already built than the roadmap's task list suggests. Worth knowing precisely, because
several Phase 3 "tasks" are really "finish and fix" rather than "write".

### Already there and usable as-is

- **A full trap frame on both architectures.** `struct x86_64_registers` (via `isr.S:52-89`)
  and `struct aarch64_registers` (`arch/aarch64/exceptions.h:6-14`) already save and restore
  the complete integer register state around a C handler. This is the single most important
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
  (`arch/x86_64/config.mk`) — see Part 2 for why that's load-bearing — and
  `-mgeneral-regs-only` on AArch64 (`arch/aarch64/config.mk`), which is why Part 9 is much
  easier on ARM than on x86.
- **FP/SIMD already enabled at EL0 on AArch64** — `CPACR_EL1.FPEN = 0b11`
  (`arch/aarch64/arch.c:11-13`), with the comment already anticipating this phase.

### Present but incomplete

- **`sys_tss.rsp0` is never set.** `tss_init()` sets `ist[0]` (`idt.c:67-68`) and nothing else.
  `rsp0` is *the* field the CPU reads on a ring-3→ring-0 trap. Left at zero, the very first
  syscall from userland pushes onto address 0 and triple-faults. This is a one-line-looking
  fix with a per-context-switch obligation behind it (Part 2).
- **The GDT has no user segments.** Only null / kernel code / kernel data / TSS
  (`idt.c:62-64`). Ring 3 needs a user code and user data descriptor, and `sysret` constrains
  *where in the GDT* they may sit (Part 6).
- **The IDT is only populated for vectors 0-31** (`idt.c:121-125`). Hardware interrupts arrive
  at vector 32 and above; there is currently no path for them at all.
- **`struct file open_files[MAX_OPEN_FILES]` is a single kernel-global table**
  (`fs/vfs/file.c:7`). File descriptors are per-*process* in POSIX. This must move into the
  task structure (Part 8).
- **Syscalls return `-1`, not `-errno`** (all of `fs/vfs/file.c`), contradicting
  [`SYSCALL_DESIGN.md`](SYSCALL_DESIGN.md) §4.

### Present and actively wrong once userland exists

These are not hypothetical — they are latent bugs that only fire when a lower exception level
starts trapping. Details and fixes in Part 2 and Part 5.

- **AArch64 vector stub saves the wrong stack pointer.** `vectors.S:21` does `mov x3, sp`,
  which on a trap *from EL0* reads SP_EL1 (the kernel stack), not the user's SP_EL0. The
  saved `regs->sp` would be meaningless and the restore at `vectors.S:125-126` would clobber
  the kernel stack pointer with it.
- **AArch64 vector stub uses `tpidrro_el0` as a scratch register** (`vectors.S:12`).
  That register is userland's read-only thread pointer — the AArch64 TLS register. Clobbering
  it on every trap corrupts thread-local storage the moment libc exists.
- **A single, global exception stack per architecture.** `x86_64_exception_stack[16384]`
  (`idt.c:37`) and `exception_stack_bottom` (`vectors.S:3-4`). One stack shared by all
  traps means no task can be suspended inside a trap handler — which is exactly what
  preemption requires (Part 2).

### Where each of these gets fixed

None of them are standalone chores to do up front — every one is folded into a step of the
plan in Part 10, at the point where it first becomes load-bearing.

| Existing problem | Fixed in |
|---|---|
| IDT only populated for vectors 0-31 | **Step 1** — needs gates ≥ 32 for the timer IRQ |
| `tss.rsp0` never set | **Step 2** |
| `vectors.S:21` saves SP_EL1 instead of SP_EL0 | **Step 2** |
| `vectors.S:12` clobbers `tpidrro_el0` | **Step 2** |
| One global exception stack per arch | **Step 2** |
| No user segments in the GDT | **Step 4**, constrained by **Step 5** ⚠️ |
| `open_files[]` is kernel-global | **Step 7** |
| Syscalls return `-1`, not `-errno` | **Step 7** |

Two consequences of that distribution are worth knowing before starting, because neither is
obvious from the step list itself.

**Step 2 is heavier than it reads.** Four of the eight land there, and three are the same
file. `vectors.S` gets substantially rewritten rather than patched: stop switching to
`exception_stack_top`, build the frame on the stack the trap arrived on, read the user stack
with `mrs x3, sp_el0` on a lower-EL entry, and drop the `tpidr_el1`/`tpidrro_el0` scratch
trick entirely. That trick only exists *because* the stub switches stacks before it has
anywhere to spill — remove the global stack and the need for it disappears with it. So the
three are one coherent change, not three.

**The GDT item straddles Steps 4 and 5, and that ordering is a trap.** Step 4 only needs user
code and data descriptors to *exist*, because `iretq` consumes whatever selector values it is
handed and doesn't care where they sit in the table. Step 5's `sysret` *computes* its
selectors from `STAR[63:48]` (Part 6), so at that point their offsets stop being free. Place
them wherever they fit at Step 4 and they get renumbered at Step 5 — so lay them out to the
`sysret` constraint the first time, even though nothing enforces it yet.

### None of these are live bugs today

Worth stating explicitly, since "actively wrong" could be misread as "currently broken". Each
of these is dormant until something traps from a lower exception level:

- `mov x3, sp` is *correct* today — every trap currently comes from EL1, where `sp` is
  already the right value.
- `tpidrro_el0` matters only once thread-local storage exists, which is Phase 4.
- The shared exception stack only corrupts if trap handlers nest or tasks are suspended
  inside one. The nearest live candidate — a nested `#PF` on the shared IST1 stack — was
  checked and isn't reachable in a way that matters: `try_handle_hhdm_mmio_fault`
  (`mm/vmm.c:206-219`) only touches page-table pages in usable RAM that Limine already
  mapped, and the one path that *could* nest is the failure `kprintf` at `mm/vmm.c:234`,
  which ends in `kpanic` regardless. The corruption would be downstream of an
  already-fatal error.

That's why they're listed as problems to fix *at a specific step* rather than as a cleanup
pass to run first.

---

## 🏗️ Part 1: The one idea that unifies this phase — the trap frame *is* the process

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

This is why the recommended design in Part 5 makes the C handler *return a frame pointer*
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
a divide-by-zero? (Answer in Part 3.)

---

## 🏗️ Part 2: Kernel stacks — the part that is quietly hard

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

## 🏗️ Part 3: Privilege levels, and the trick of entering userland

### What "unprivileged" actually means

Ring 3 (x86_64) and EL0 (AArch64) are CPU modes where:
- privileged instructions (`lgdt`, `msr`, `hlt`, `mov cr3`, ...) fault instead of executing,
- I/O port access is denied (x86, via `rflags.IOPL` and the TSS I/O bitmap),
- and — the important one — **page table entries without the user bit are inaccessible**.

That last point is the one that matters most here, and it's already built. `VMM_USER` maps to
PTE bit 2 on x86_64 (`arch/x86_64/arch.c:39`) and to `AP[1]` on AArch64
(`arch/aarch64/arch.c:76-78`). Kernel mappings simply don't set it, so a user process
dereferencing a kernel address takes a page fault rather than reading kernel memory.

> [!NOTE]
> Worth checking during this phase: on x86_64, kernel pages must *also* not be user-accessible
> in the upper half, and SMEP/SMAP (CR4 bits 20/21) are the hardware backstop that stops the
> *kernel* from accidentally executing or reading user memory. Enabling them is cheap and
> turns a class of silent bugs into immediate faults. SMAP requires `stac`/`clac` around
> deliberate user accesses, so it pairs with Part 8's `copy_from_user`.

### Entering ring 3 / EL0

Per Part 1: fabricate a trap frame and return from it. No special instruction.

**x86_64** — build a stack that `iretq` will consume. `iretq` pops, in order:
`rip`, `cs`, `rflags`, `rsp`, `ss`. Setting `cs` to the user code selector with RPL 3 and
`ss` to the user data selector with RPL 3 is what makes the CPU land in ring 3. `rflags`
should have `IF` set (0x202) or the process runs with interrupts disabled and can never be
preempted.

**AArch64** — `eret` uses `ELR_EL1` as the return address and `SPSR_EL1` as the restored
processor state. Setting `SPSR_EL1.M[3:0] = 0b0000` (EL0t) is what makes it land at EL0. The
`D`,`A`,`I`,`F` mask bits in SPSR must be *clear* so interrupts are enabled at EL0. The user
stack goes in `SP_EL0` via `msr sp_el0, x`.

Both frames are already the exact structs the existing stubs restore from, which is the
payoff of Part 1: the "enter userland" code path is *the scheduler picking a task that has
never run before*, not a special case.

Good question to chase before coding: what stops a user process from just executing `iretq`
itself with a ring-0 `cs` and promoting itself? (It's not that `iretq` is privileged.)

---

## 🏗️ Part 4: Timers and interrupts — where the two arches diverge most

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

Also required: `daifclr` to actually unmask IRQs at EL1, and the IRQ vector slots
(`VECTOR_ENTRY 5` for current-EL, `9` for lower-EL) currently fall into the same
unhandled-exception panic as everything else in `exceptions.c:41-53`.

### One shared abstraction

Both sides should end up behind something like:

```c
// include/arch/arch.h
void     arch_timer_init(uint32_t hz);
uint64_t arch_timer_ticks(void);
void     arch_irq_enable(void);
void     arch_irq_disable(void);
```

with the tick counter in generic code, feeding `sys_clock_gettime` and `sys_nanosleep` later.
Keeping the *policy* (how many ticks per slice, when to reschedule) out of `arch/` matters
more than the exact function list.

---

## 🏗️ Part 5: The scheduler

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
its `frame` is just a fabricated one (Part 3). Second, it forces the ordering that's easy to
get wrong: **switch the address space before returning to the frame, but note that the kernel
stack you're currently standing on must stay mapped across that switch.** It does, because
kernel mappings are shared by every context (`arch_vmm_new_context_root`) — but that's a real
invariant worth writing down rather than a happy accident.

The limitation, stated honestly: this handles *preemptive* switches only. A task that blocks
inside a syscall (waiting on a pipe, on a child, on input) needs the voluntary path, and that
does need a real `switch_to`. That's why Part 10 puts blocking after `wait4` rather than
pretending it's free.

### Where reschedule is allowed to happen

Cleanest rule for a first implementation: **reschedule only on the way out of a trap, and only
when returning to a point that is safe to leave.** Set a `need_resched` flag in the timer
handler, and act on it at a single place near the end of the trap path. Rescheduling from
arbitrary depths inside kernel code is what makes a kernel "preemptible", and it requires
locking discipline this kernel doesn't have yet. Non-preemptible-kernel first is the correct
scope.

---

## 🏗️ Part 6: The syscall entry path

### Why not just use an exception

`int 0x80` / a software-generated exception works and would reuse the stubs verbatim. It's
also slow, and — more importantly — it isn't what a Linux-ABI userland emits.
[`SYSCALL_DESIGN.md`](SYSCALL_DESIGN.md) §3 already commits to `syscall`/`sysret` and `svc`,
which is the right call: a compiled BusyBox will emit `syscall` whether or not the kernel
supports it.

### AArch64: nearly free

`svc #0` is a synchronous exception from EL0 — it lands in the **existing vector table**, at
`VECTOR_ENTRY 8` (Lower EL, AArch64, Synchronous). The dispatch is a check in
`aarch64_exception_handler`: `ESR_EL1.EC == 0x15` means SVC from AArch64. Arguments are
already in `regs->x[0..5]`, the number in `regs->x[8]`, and the return value goes back by
writing `regs->x[0]` — the stub restores it on the way out.

That's it. There is no separate entry path to write on AArch64. (There is still the
Part 0 stub-correctness work, which becomes mandatory here.)

### x86_64: a genuinely new entry path

`syscall` is fast precisely because it does almost nothing: it puts the return address in
`rcx`, `rflags` in `r11`, loads `cs`/`ss` from `IA32_STAR`, and jumps to `IA32_LSTAR`. It does
**not** switch stacks. The handler begins executing *on the user stack*, in ring 0.

That's the crux of the whole x86 syscall path, and every design decision follows from it: the
entry code must find a kernel stack while holding no free register and no trusted stack.

The standard answer is `swapgs` + `IA32_KERNEL_GS_BASE`: `swapgs` atomically exchanges
`GS_BASE` with a kernel-supplied MSR value, giving one `%gs:`-relative pointer to work from
without clobbering any general register.

```asm
syscall_entry:
    swapgs                          /* gs now points at per-cpu data */
    movq %rsp, %gs:cpu_user_rsp     /* stash the user stack */
    movq %gs:cpu_kernel_rsp, %rsp   /* switch to this task's kernel stack */
    /* ...build a trap frame, call C dispatch, unwind... */
    swapgs
    sysretq
```

Single-CPU shortcut (worth knowing, and worth *not* taking): a plain
`movq %rsp, saved_rsp(%rip)` would work today with one CPU. Recommendation is to use `swapgs`
from the start anyway — it's the same amount of code, and it's the piece that would otherwise
have to be redesigned rather than extended when a second CPU appears.

MSRs to program in `arch_init()`:

| MSR | Purpose |
|---|---|
| `IA32_EFER.SCE` (bit 0) | Enable the `syscall` instruction at all. |
| `IA32_LSTAR` | Entry point address. |
| `IA32_STAR` | Kernel and user segment selector bases. |
| `IA32_FMASK` | Bits cleared from `rflags` on entry — **must include `IF`**. |
| `IA32_KERNEL_GS_BASE` | The value `swapgs` swaps in. |

### The GDT layout constraint

`sysret` does not read the GDT — it *computes* selectors: returning to 64-bit mode sets
`CS = STAR[63:48] + 16` and `SS = STAR[63:48] + 8`, both forced to RPL 3. So the GDT must be
laid out so that the user data descriptor sits 8 bytes after the STAR user base and the
64-bit user code descriptor 16 bytes after it. Correspondingly `syscall` sets
`CS = STAR[47:32]`, `SS = STAR[47:32] + 8`, so kernel code must be immediately followed by
kernel data — which `idt.c:63-64` already satisfies (0x08 then 0x10).

The current GDT ends with a 16-byte TSS descriptor at 0x18, so the user entries go after it
and the STAR user base is chosen to land correctly relative to them. Getting this off by 8
produces a #GP on the *return* from the first syscall, with a fault address that points at
perfectly good user code — a memorably confusing hour if the constraint isn't known in
advance.

> [!NOTE]
> `sysret` has a genuine security erratum worth reading about before relying on it: if `rcx`
> holds a non-canonical address, `sysretq` faults **in ring 0 on the user stack**. Linux
> checks canonicality and falls back to `iretq`. Not urgent for a hobby kernel with trusted
> binaries; worth a comment in the code so the decision is deliberate rather than unknowing.

### Dispatch

Per `SYSCALL_DESIGN.md` §2, a flat table indexed by number. Worth noting: the syscall numbers
**differ between the two architectures** (x86_64 `write` is 1; AArch64 `write` is 64), so the
table is per-arch even though every handler is shared. Generating both from one list of
`(name, handler)` pairs plus two number headers avoids the two tables drifting.

---

## 🏗️ Part 7: The ELF loader and the initial user stack

### The minimum viable loader

For a *static, non-PIE* ELF64 executable, the loader needs to understand exactly two
structures: the header (`Elf64_Ehdr`) and the program headers (`Elf64_Phdr`). Section headers,
the symbol table, and relocations are all irrelevant to execution — they're linker and
debugger concerns.

The algorithm:

1. Validate `e_ident` magic (`\x7fELF`), class (64-bit), and `e_machine` against the running
   architecture. Rejecting an x86 binary on ARM with a clear message beats debugging a fault
   in fabricated user code.
2. For each program header with `p_type == PT_LOAD`:
   - allocate and map `[p_vaddr, p_vaddr + p_memsz)` into the new context, page-aligned
     outward at both ends,
   - copy `p_filesz` bytes from the file at `p_offset`,
   - **zero the remaining `p_memsz - p_filesz` bytes.** That difference is `.bss`. Skip it and
     the program starts with garbage globals — which usually *looks* like working code until
     it doesn't.
   - translate `p_flags` (`PF_R`/`PF_W`/`PF_X`) into `VMM_USER | VMM_WRITABLE | VMM_NO_EXECUTE`.
3. `e_entry` is the initial `rip`/`elr`.

### The non-obvious part: you are writing into a foreign address space

`vmm_map_page` maps into the *new* context, but the kernel is still executing in the old one,
so `memcpy` to `p_vaddr` would write to whatever the current context has at that address —
usually nothing, occasionally something important.

Two ways out:

- Switch to the new context first, then copy. Works (kernel mappings are present in every
  context), but means the loader runs in a half-built address space.
- **Copy through the HHDM instead**: `pmm_alloc(1)`, `memcpy` into `pmm_phys_to_virt(phys)`,
  then `vmm_map_page(new_ctx, vaddr, phys, flags)`. The physical page is writable through the
  kernel's direct map regardless of which context is live, and the mapping's permissions
  (including read-only text) are then set *after* the content is in place.

The second is recommended — it's simpler, avoids a context switch inside the loader, and it's
the same trick that will be needed for `fork`'s copy-on-write later. Note the ordering benefit:
a read-only segment can be genuinely read-only from the moment it's mapped.

Partial pages need care: two `PT_LOAD` segments can share a page (typically the end of `.text`
and the start of `.rodata`/`.data`), so "allocate a fresh page per page-aligned address"
must tolerate an already-mapped page and copy into it rather than replacing it.

### The initial user stack

Userland doesn't start at `main` — it starts at `_start`, which expects a very specific stack
layout. At the moment control transfers, `rsp`/`sp` points at:

```
      low addresses
      ┌──────────────────┐  ← initial rsp / sp   (must be 16-byte aligned)
      │ argc             │
      │ argv[0] ptr      │
      │ ...              │
      │ NULL             │
      │ envp[0] ptr      │
      │ ...              │
      │ NULL             │
      │ auxv[0].type/val │   ← key/value pairs
      │ ...              │
      │ AT_NULL / 0      │
      │ (string data)    │   ← what argv/envp actually point at
      └──────────────────┘
      high addresses
```

**auxv** (the auxiliary vector) is the kernel→libc channel: page size, the ELF's own program
header location, the entry point, a seed for stack-protector randomness. A hand-written
assembly `init` needs none of it — terminating with `AT_NULL` alone is fine. A real libc in
Phase 4 will need at least `AT_PAGESZ`, `AT_PHDR`, `AT_PHNUM`, `AT_PHENT`, `AT_ENTRY`, and
`AT_RANDOM`. Building the array as a small table from the start, even with two entries, makes
that a data change rather than a redesign.

The 16-byte alignment is not optional on x86-64: SSE instructions in libc assume it, and the
resulting `#GP` on a `movaps` is one of the classic first-userland-process bugs.

### Getting a test binary without a libc

`zig cc` is already the toolchain (`arch/*/config.mk`), and it can target
`x86_64-linux-none` / `aarch64-linux-none` freestanding. So the first `/bin/init` can be a
~30-line C file with inline-asm syscall wrappers, no libc, statically linked — added to
`initrd/` and picked up by the existing `initrd.tar` rule in the top-level `Makefile`.

That matters for sequencing: **userland can be tested long before a C library exists.** The
first init that writes "hello from ring 3" and calls `exit` validates the ELF loader, the
privilege transition, the syscall path, and the fd table in one shot.

---

## 🏗️ Part 8: The syscall implementations — what changes in existing code

The VFS syscalls already exist and work. Making them *user*-callable changes three things.

### 1. File descriptors become per-process

`fs/vfs/file.c:7` holds `static struct file open_files[MAX_OPEN_FILES]` — one global table.
POSIX requires each process to have its own fd space (both processes get fd 0, 1, 2), and
`fork` must duplicate the table while *sharing* the underlying open-file descriptions (so a
parent and child share a file offset — that's specified behaviour, not an accident).

That's the classic three-layer structure worth adopting deliberately:

```
task->fds[fd]  ──►  struct file (offset, flags, refcount)  ──►  struct vfs_node
   per-process           shared by dup/fork                       shared by everyone
```

The existing `struct file` already has `ref_count` (`include/fs/vfs/file.h:8-13`), which was
built for `close` but is exactly the field this needs. `allocate_fd()`'s scan
(`file.c:14-21`) moves into the task's array.

### 2. Pointers from userland are hostile

`sys_read(fd, buf, count)` currently writes straight through `buf` (`file.c:108`). When `buf`
comes from a user register, it may be a kernel address, unmapped, or straddling a boundary.

The check is cheap: the address range must lie entirely below `VMM_ADDR_SPLIT`
(`include/mm/vmm.h:25` — already defined) and must not wrap. Whether to also verify the pages
are mapped is a real design choice: Linux doesn't, it just takes the page fault and recovers
via a fixup table. Recommendation for now is the simple version — bounds-check, then let a
genuine fault be a fault — with `copy_from_user`/`copy_to_user` existing as *functions* from
day one even if their bodies are `memcpy` plus a bounds check, so the call sites are already
right when they get smarter.

### 3. Error returns become `-errno`

`SYSCALL_DESIGN.md` §4 commits to it, current code returns `-1` everywhere. This is
mechanical but touches every function in `file.c`, and it needs `include/errno.h` to exist. Do
it in one pass rather than mixing conventions — a half-converted error space is worse than
either convention alone.

Also worth noting: `sys_readdir` (`include/core/syscalls.h:22`) is **not** a Linux syscall.
Linux uses `getdents64` with a packed variable-length `struct linux_dirent64`. It can stay as
an internal helper, but the ABI-facing syscall will need to be `getdents64` before BusyBox's
`ls` works. Not urgent; worth knowing it's a rename-plus-reformat waiting in Phase 4.

---

## 🏗️ Part 9: FPU / SIMD state

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

## 🪜 Part 10: Order of work

Same discipline as Phase 2: every step boots green on its own, and each one is independently
verifiable. The ordering deliberately separates "interruption" from "distrust" — get
preemption working with *kernel* threads first, where a bug prints a message instead of
resetting the machine.

**Step 1 — Timer and interrupts, no scheduler.** → [`PHASE3_STEP1_TIMER.md`](PHASE3_STEP1_TIMER.md)
`arch_timer_init`, IDT gates above 31 + PIT + PIC remap on x86_64, GIC + Generic Timer on
AArch64. Acceptance: a tick counter increments and prints once per second on both arches, and
the kernel still reaches `arch_shutdown()`. This is the largest arch-specific chunk and it
carries no scheduler risk — do it alone. Broken into nine individually verifiable baby steps
in its own working document.

**Step 2 — `struct task`, per-task kernel stacks, and the stub contract change.**
The four-line stub change from Part 5 (handler returns a frame), `tss.rsp0` / `SP_EL1`
maintenance, and the AArch64 `vectors.S` correctness fixes from Part 0. Still one task, so
the "scheduler" always returns the frame it was given. Acceptance: nothing observable changes
— which is the point. A green boot here means the plumbing is right before it's load-bearing.

**Step 3 — Two kernel threads, round-robin.**
Second task with a fabricated frame whose `rip` points at a kernel function, both printing.
Acceptance: interleaved output. **This is the milestone where preemption is real**, and it
happens entirely in ring 0 where debugging is still easy.

**Step 4 — Ring 3 / EL0, with a hand-fabricated user program.**
Skip the ELF loader for now: `memcpy` a few hand-assembled instructions into a user page and
enter it. Acceptance: the process runs and then faults on a privileged instruction, and the
existing unhandled-exception dump reports it from CS=0x33 / SPSR EL0t. **Faulting is the
success criterion here** — it proves the privilege drop actually happened.

**Step 5 — The syscall entry path.**
`svc` dispatch on AArch64 (small), `syscall`/`sysret` + MSRs + GDT layout on x86_64 (not
small). Wire exactly one syscall — `write` to the console. Acceptance: the hand-made user
program prints a string and exits cleanly.

**Step 6 — The ELF loader and a real `/bin/init`.**
Loader per Part 7, plus a freestanding `init.c` built by `zig cc` and added to `initrd.tar`.
Acceptance: `/bin/init` loads from the ramfs and prints from ring 3. At this point Phase 3's
stated goal is met.

**Step 7 — The real syscall surface.**
Per-process fd tables, `copy_from_user`, `-errno` conversion, and `open`/`read`/`close`/
`exit`/`getpid` reachable from userland. Acceptance: an init that opens `/root/hello.txt` and
prints its contents — the whole stack, ring 3 to ramfs and back.

**Step 8 — `fork`, `execve`, `wait4`, and voluntary switching.**
The genuinely hard step, and the one where `switch_to` becomes unavoidable (a parent blocking
in `wait4` is not a timer-driven switch). Reasonable to split into its own working document
once Steps 1-7 have taught what the task structure actually needs to hold.

**FPU (Part 9) slots in wherever a second *user* task first exists** — likely with Step 8. The
`-mno-sse` build change can land any time and is worth doing early since it's a one-line
change with a compile-time-visible blast radius.

---

## ⚖️ Part 11: Design decisions to make deliberately

Recommendations given, but these are the calls worth making consciously rather than by
default. Several of these are the "open decisions" in `SYSCALL_DESIGN.md` §13.

| Decision | Options | Recommendation |
|---|---|---|
| x86 interrupt controller | PIC+PIT vs LAPIC+IOAPIC | PIC+PIT — replaceable, unblocks Step 1 today |
| x86 syscall entry | `syscall`/`sysret` vs `int 0x80` | `syscall` only; the ABI target requires it |
| Finding the kernel stack on x86 | `swapgs` vs single-CPU global | `swapgs` — same effort, survives SMP |
| AArch64 GIC base | DTB parse vs hard-code QEMU virt | Hard-code with a loud comment; parse in Phase 7 |
| Kernel preemptibility | preemptible vs not | Not preemptible — reschedule only on trap exit |
| Kernel vector registers | `-mno-sse` vs eager save | `-mno-sse`, matching AArch64's existing stance |
| Context switch mechanism | frame-swap vs `switch_to` | Frame-swap first; add `switch_to` at Step 8 |
| ELF segment copy | switch context vs copy via HHDM | HHDM — no context switch, permissions applied last |
| `#PF` stack | keep IST1 vs per-task stack | Per-task; IST1 stays for `#DF` only |

---

## 🕳️ Part 12: Traps worth knowing about in advance

Collected because each one has a symptom that points somewhere other than the cause.

- **`tss.rsp0` unset** → triple fault on the first user→kernel trap. No output at all.
- **GDT/STAR misalignment** → `#GP` on `sysret`, with a fault address inside valid user code.
- **`IF` not set in the fabricated `rflags`** → the first user process runs but is never
  preempted, and looks like a scheduler bug.
- **`IA32_FMASK` not clearing `IF`** → interrupts stay enabled during syscall entry, before a
  kernel stack is established. Rare, catastrophic, non-deterministic.
- **Missing EOI** → exactly one timer interrupt fires, ever. Preemption "works once".
- **`p_memsz` vs `p_filesz`** → uninitialised globals; code that works until it doesn't.
- **User stack not 16-byte aligned** → `#GP` on the first `movaps` inside libc, far from the
  cause.
- **`tpidrro_el0` clobbered by the AArch64 stub** (`vectors.S:12`) → TLS corruption that only
  appears once libc exists in Phase 4, long after the stub was last touched.
- **Kernel stack overflow** → silent heap corruption. Guard values make it a clean panic.
- **Reusing one exception stack across tasks** → corruption proportional to how *well* the
  scheduler is working, which is a memorably bad debugging experience.

---

## 📁 Critical files

Existing, to be modified:

- ⬜ `kernel/arch/x86_64/idt.c` — `rsp0`, user GDT entries, IDT gates above 31, `#PF` off IST
- ⬜ `kernel/arch/x86_64/isr.S` — handler returns a frame; restore from it
- ⬜ `kernel/arch/aarch64/vectors.S` — per-task stack instead of the global one, correct
  `SP_EL0` save, drop the `tpidrro_el0` scratch use, handler returns a frame
- ⬜ `kernel/arch/aarch64/exceptions.c` — SVC (`EC=0x15`) and IRQ dispatch before the panic path
- ⬜ `kernel/include/arch/arch.h` — timer/IRQ/kernel-stack primitives, common `trap_frame` name
- ⬜ `kernel/fs/vfs/file.c` + `include/fs/vfs/file.h` — fd table into `struct task`, `-errno`
- ⬜ `kernel/core/main.c` — start the scheduler and `/bin/init` instead of shutting down
- ⬜ `kernel/arch/x86_64/config.mk` — `-mno-sse` and friends

New:

- ⬜ `kernel/arch/x86_64/pic.c`, `pit.c`, `syscall_entry.S`
- ⬜ `kernel/arch/aarch64/gic.c`, `timer.c`
- ⬜ `kernel/proc/task.c`, `sched.c`, `elf.c` — `kernel/Makefile:30` globs
  `core mm drivers fs`, so **a new top-level directory needs adding to that find**
- ⬜ `kernel/syscall/syscall.c` — the dispatch table
- ⬜ `kernel/include/errno.h`, `kernel/include/asm/unistd.h` (per `SYSCALL_DESIGN.md` §8)
- ⬜ `initrd/bin/init.c` + a `Makefile` rule building it with `zig cc`

## 📝 Docs to keep in step

- ✅ `ROADMAP.md` — Phase 3 marked not-started with this doc linked, and its task list rewritten
  around the steps in Part 10. Also documents the `doc/` vs `doc/archive/` split.
- ✅ `README.md` — roadmap section points here as the live working document, and explains the
  archive convention. The subsystem table needs revisiting once Phase 3 code actually lands.
- ⬜ `SYSCALL_DESIGN.md` — §13's open decisions answered from Part 11 as they're settled
- ⬜ The subsystem table in `README.md` — one row each for the scheduler, the syscall layer and
  the ELF loader, once they exist

## 🪜 Verification

The `core/test/` suite can't reach most of this — a scheduler and a privilege transition
aren't assertable from inside a single-threaded kernel self-test. The honest split:

**Still unit-testable** (add to `core/test/`): ELF header validation and rejection of bad
binaries, the `copy_from_user` bounds check including the wraparound case, fd table allocation
and exhaustion, and the initial-stack builder's layout and alignment given known argv/envp.

**Only observable by booting**: preemption (interleaved output from Step 3), the privilege
drop (Step 4's deliberate fault), and the syscall round trip (Step 5). These need a *stated
expected output* per step rather than assertions — which is what the acceptance criteria in
Part 10 are for.

Worth adding early, since it costs little and pays across every step: a `task` debug command
or boot-time dump printing each task's pid, state, and kernel stack usage. Most of Phase 3's
bad days are "which task was actually running when this happened".
