# Working Document: Phase 4 — Privilege: Ring 3 / EL0 and the Syscall Boundary [STATUS: COMPLETE ✅]

> [!NOTE]
> **Where this stands.** Both Steps are complete on both architectures. Step 1 (the privilege
> drop) — see [`PHASE4_STEP1_RING3_EL0.md`](PHASE4_STEP1_RING3_EL0.md). Step 2 (the syscall entry
> path) — see [`PHASE4_STEP2_SYSCALL.md`](PHASE4_STEP2_SYSCALL.md): `write(1, ...)` from ring 3 /
> EL0 reaches the console on both architectures, proven end to end, plus bounds/adversarial checks
> on both. Phase 4's payoff — a program the kernel does not trust calls `write` and the string
> appears on the console — is met.
>
> This design was written as part of the original single-document Phase 3 plan and split out
> when that phase was broken into three along the capability boundaries it had already
> identified. The starting-line inventory, the trap-frame model it depends on, and the
> scheduler live in [`PHASE3_PREEMPTION.md`](archive/PHASE3_PREEMPTION.md).

> [!NOTE]
> Mentor-mode reminder (see the note at the top of [`../README.md`](../README.md)): this is a
> design reference to code from by hand, not code to paste in. Snippets are illustrative and
> deliberately incomplete.

---

## 🎯 What "done" looks like

> A program the kernel does not trust — running at ring 3 / EL0, faulting if it attempts a
> privileged instruction — calls `write(1, "hello from ring 3\n", 18)`, the string appears on
> the console, and the process exits cleanly.

The program is a hand-assembled blob `memcpy`'d into a user page. **There is no ELF loader
yet** — that's Phase 5, deliberately. A bug in the privilege transition and a bug in an ELF
loader both present as "it faulted somewhere in user memory", and separating them is worth a
phase boundary.

---

## 🧭 The capability this phase adds: distrust

Phase 3 gave the kernel interruption and multiplicity — it can be stopped mid-instruction and
it can switch between threads. Every one of those threads is still kernel code the kernel
wrote and trusts completely.

This phase introduces the first code the kernel must treat as hostile. Two mechanisms, and
they're the same mechanism twice:

- **Getting there** — drop to a lower privilege level, which turns out to need no new
  instruction at all.
- **Coming back** — a deliberate, fast, one-way door back into the kernel, which on one
  architecture is nearly free and on the other is the single fiddliest piece of assembly in
  the whole project.

| Term | One-line version | Chapter |
|---|---|---|
| **Ring 3 / EL0** | The unprivileged CPU mode userland runs in; privileged instructions fault there. | 1 |
| **`iretq` / `eret`** | The instruction that drops to userland — the *same* one that returns from an exception. | 1 |
| **SMEP / SMAP** | Hardware backstop stopping the *kernel* from executing or reading user memory by accident. | 1 |
| **`syscall`/`sysret`, `svc`** | The fast, dedicated user→kernel entry that isn't an exception. | 2 |
| **`swapgs` / `KERNEL_GS_BASE`** | How the trap handler finds the kernel's own data with *no* usable register or stack. | 2 |
| **The STAR constraint** | `sysret` computes its selectors, so the GDT's user entries are not free to sit anywhere. | 2 |

---

## 🏗️ Chapter 1: Privilege levels, and the trick of entering userland

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
> deliberate user accesses, so it pairs with
> [`PHASE5_LOADING.md`](PHASE5_LOADING.md) Chapter 2's `copy_from_user`.

### Entering ring 3 / EL0

Per [`PHASE3_PREEMPTION.md`](archive/PHASE3_PREEMPTION.md) Chapter 1: fabricate a trap frame and return
from it. No special instruction.

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
payoff of Phase 3's Chapter 1: the "enter userland" code path is *the scheduler picking a task
that has never run before*, not a special case.

Good question to chase before coding: what stops a user process from just executing `iretq`
itself with a ring-0 `cs` and promoting itself? (It's not that `iretq` is privileged.)

---

## 🏗️ Chapter 2: The syscall entry path

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
stub-correctness work in Phase 3's Chapter 0, which becomes mandatory here.)

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

> [!IMPORTANT]
> **This constraint straddles both Steps, and that ordering is a trap.** Step 1 only needs user
> code and data descriptors to *exist*, because `iretq` consumes whatever selector values it is
> handed and doesn't care where they sit in the table. Step 2's `sysret` *computes* its
> selectors, so at that point their offsets stop being free. Place them wherever they fit at
> Step 1 and they get renumbered at Step 2 — so lay them out to the `sysret` constraint the
> first time, even though nothing enforces it yet.

> [!NOTE]
> `sysret` has a genuine security erratum worth reading about before relying on it: if `rcx`
> holds a non-canonical address, `sysretq` faults **in ring 0 on the user stack**. Linux
> checks canonicality and falls back to `iretq`. Not urgent for a hobby kernel with trusted
> binaries; worth a comment in the code so the decision is deliberate rather than unknowing.

### Dispatch

Per [`SYSCALL_DESIGN.md`](SYSCALL_DESIGN.md) §2, a flat table indexed by number. Worth noting:
the syscall numbers **differ between the two architectures** (x86_64 `write` is 1; AArch64
`write` is 64), so the table is per-arch even though every handler is shared. Generating both
from one list of `(name, handler)` pairs plus two number headers avoids the two tables
drifting.

---

## 🪜 Chapter 3: The Steps

Two Steps, and they must stay married — Step 1's success criterion is a *crash*, which is a
fine place to end a Step and a terrible place to end a phase.

**⬜ Step 1 — Ring 3 / EL0, with a hand-fabricated user program.**
→ [`PHASE4_STEP1_RING3_EL0.md`](PHASE4_STEP1_RING3_EL0.md)
Skip the ELF loader: `memcpy` a few hand-assembled instructions into a user page and enter it.
Needs user segments in the GDT on x86_64, laid out to `sysret`'s constraint even though
nothing enforces it until Step 2. Acceptance: the process runs and then faults on a privileged
instruction, and the existing unhandled-exception dump reports it from a user `CS` / SPSR EL0t.
**Faulting is the success criterion here** — it proves the privilege drop actually happened.
Broken into six individually verifiable Parts (plus an optional SMEP one) in its own working
document, which also computes this tree's actual user selectors — they are **not** Linux's
`0x33`/`0x2b`, because the TSS sits at 0x18 here and shifts everything by a slot.

**⬜ Step 2 — The syscall entry path.**
→ [`PHASE4_STEP2_SYSCALL.md`](PHASE4_STEP2_SYSCALL.md)
`svc` dispatch on AArch64 (small); `syscall`/`sysret` + `swapgs` + the MSRs + the GDT layout on
x86_64 (not small). Wire exactly one syscall — `write` to the console. Acceptance: the
hand-made user program prints a string and exits cleanly. **Phase 4's stated goal is met
here.**

---

## ⚖️ Chapter 4: Design decisions to make deliberately

| Decision | Options | Recommendation |
|---|---|---|
| x86 syscall entry | `syscall`/`sysret` vs `int 0x80` | `syscall` only; the Linux ABI target requires it |
| Finding the kernel stack on x86 | `swapgs` vs single-CPU global | `swapgs` — same effort, survives SMP |
| `sysret` erratum | check canonicality vs ignore | Ignore for now, but *comment* it, so the risk is accepted rather than unknown |
| SMEP / SMAP | enable now vs later | Enable SMEP now (free); SMAP pairs with Phase 5's `copy_from_user` |

---

## 🕳️ Chapter 5: Traps worth knowing about in advance

Each has a symptom that points somewhere other than the cause.

- **`tss.rsp0` unset** → triple fault on the first user→kernel trap. No output at all. Phase 3
  Step 2 sets it; this is the phase where forgetting it becomes fatal.
- **⚠️ `tss.rsp0` set, but pointing into a stack that is already in use** → *not* a triple
  fault, which is what makes it worse. Phase 3 Step 2 left task 0 running on the stack Limine
  handed it — you cannot move a stack you are standing on — so `task_init_boot()` records the
  live `sp` as its `kernel_stack_top`, and that is what `arch_set_kernel_stack()` writes. The
  CPU never reads `rsp0` while nothing traps from ring 3, so it is dormant until **this phase's
  Step 1**, where the first trap back from EL0 builds its frame on top of task 0's live locals
  and corrupts whatever C frame was mid-flight. Symptom: plausible-looking garbage in kernel
  variables after the first syscall, pointing nowhere near the cause. **Fix before entering
  ring 3:** whichever task returns from EL0 must own a real `kstack_alloc()` stack, not the
  boot stack.
- **GDT/STAR misalignment** → `#GP` on `sysret`, with a fault address inside valid user code.
- **`IF` not set in the fabricated `rflags`** → the first user process runs but is never
  preempted, and looks like a scheduler bug.
- **`IA32_FMASK` not clearing `IF`** → interrupts stay enabled during syscall entry, before a
  kernel stack is established. Rare, catastrophic, non-deterministic.
- ~~**AArch64 `vectors.S` saving the wrong `sp`**~~ **Fixed in Phase 3 Step 2 Part B2.** Lower-EL
  entries now read the interrupted `sp` from `SP_EL0`, and the exit path decides where to put it
  back by reading the vector type *out of the frame* rather than from the trap that arrived —
  because after a context switch those are two different tasks. The related hazard is still
  live and worth re-reading before Step 1: **never write a lower-EL entry's saved `sp` into
  SP_EL1**, which hands userland the kernel's stack pointer for the next trap.
- ~~**`tpidrro_el0` clobbered by the AArch64 stub**~~ **Fixed in Phase 3 Step 2 Part B2** — the
  scratch-register trick disappeared along with the global exception stack that forced it, so
  the AArch64 TLS register is untouched by the trap path.

---

## 📁 Critical files

Existing, to be modified:

- ⬜ `kernel/arch/x86_64/idt.c` — user code/data GDT entries, laid out to the STAR constraint
- ⬜ `kernel/arch/x86_64/arch.c` — the five MSRs, and SMEP/SMAP in CR4
- ⬜ `kernel/arch/aarch64/exceptions.c` — SVC dispatch on `ESR_EL1.EC == 0x15`
- ⬜ `kernel/proc/task.c` — fabricating a frame whose `cs`/`spsr` names a lower privilege level

New:

- ⬜ `kernel/arch/x86_64/syscall_entry.S` — the `swapgs` entry stub
- ⬜ `kernel/syscall/syscall.c` — the dispatch table (needs adding to `kernel/Makefile`'s `find`)
- ⬜ `kernel/include/asm/unistd.h` — per-arch syscall numbers, per `SYSCALL_DESIGN.md` §8

---

## 🪜 Verification

Neither Step is reachable from `core/test/` — a privilege transition isn't assertable from
inside a single-threaded kernel self-test. Both are observed by booting, with a stated expected
output:

| Step | Expected |
|---|---|
| 1 | An unhandled-exception dump reporting `CS=0x33` (x86_64) or SPSR EL0t (AArch64) |
| 2 | `hello from ring 3` on the console, then a clean exit and shutdown |

The one genuinely unit-testable piece arrives with Phase 5: the `copy_from_user` bounds check,
including the wraparound case.
