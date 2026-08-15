# Working Document: Phase 4 Step 2 — The Syscall Entry Path [STATUS: NOT STARTED ⬜]

> [!NOTE]
> **Phase 4 Step 2**, from [`PHASE4_PRIVILEGE.md`](PHASE4_PRIVILEGE.md) Chapter 3, expanded into
> individually verifiable Parts. It draws on Chapter 2 (the entry path itself) and Chapter 5 (the
> traps). Read those for the *why*; this document is the *how*, in order. It also leans on
> [`SYSCALL_DESIGN.md`](SYSCALL_DESIGN.md) for the numbering/calling-convention/dispatch
> conventions this and every later syscall must follow.
>
> This Step **must stay married to Step 1** — [`PHASE4_STEP1_RING3_EL0.md`](PHASE4_STEP1_RING3_EL0.md)
> ended on a *crash*, which was the right acceptance criterion there and would be the wrong one
> here. Everything Step 1 built — the GDT's user segments laid out to the `sysret` constraint,
> `arch_task_init_user_frame()`, `task_create_user()`, the two user pages — is reused as-is.
> Nothing here changes it.

> [!NOTE]
> Mentor-mode reminder (see the note at the top of [`../README.md`](../README.md)): this is a
> design reference to code from by hand, not code to paste in. Signatures, struct fields and
> constants are given exactly; the bodies are yours. Where a register value or MSR behaviour is
> stated, confirm it against the manual.

---

## 🎯 What "done" looks like

> The user task from Step 1 executes `write(1, "hello from ring 3\n", 18)` from ring 3 / EL0.
> The string appears on the console. The kernel never trusted the blob, and the blob never
> touched anything it wasn't handed a door to.

**Only `write` is required.** [`PHASE4_PRIVILEGE.md`](PHASE4_PRIVILEGE.md) Chapter 3 is explicit:
*"Wire exactly one syscall — `write` to the console."* The phase overview's looser wording
("prints a string and exits cleanly") shouldn't be read as "implement `exit` too" — there is no
`exit` syscall in this Step's scope, and Phase 7 owns real process lifetime (same caveat Step 1's
own document closed on). **Recommendation: after the write returns, have the blob execute the
exact same privileged instruction Step 1 used** (`hlt` / `mrs x0, sctlr_el1`) and let it fault
into the same register dump as before — "prints, then proves the machine is still fine" is a
cleaner, honestly-scoped acceptance criterion than an "exit" this Step has no mechanism for.

**What is explicitly *not* in this step:**

- ❌ **No `open`/`read`/`close` reachable from ring 3.** `sys_open()`/`sys_read()`/`sys_close()`
  already exist (`fs/vfs/file.c`) and already work — they're used internally by `tar_load()`
  during boot. This Step doesn't touch them; it only builds the *door* for `write` to reach
  `sys_write()`. Reaching the rest of the syscall surface from userland is
  [`PHASE5_LOADING.md`](PHASE5_LOADING.md) Step 2's job.
- ❌ **No real `/dev/console`.** `console_putc()` (`core/console.c`) writes straight to the
  serial/framebuffer with no VFS node behind it — there is no file descriptor 0/1/2 pre-opened
  anywhere today (`open_files[]` starts fully closed). This Step needs `fd 1` to *mean* something;
  see Decision 2 below. A real device node with TTY behavior is
  [`PHASE6...`](ROADMAP.md)'s job (`/dev/console`, line discipline).
- ❌ **No `-errno` convention.** [`SYSCALL_DESIGN.md`](SYSCALL_DESIGN.md) §4 wants `-errno`
  returns; `sys_write()` today returns bare `-1`. Pass whatever it returns straight through — the
  blob doesn't inspect it. Fixing every syscall's return convention is Phase 5 Step 2's job, along
  with the `-errno` values living in `errno.h`.
- ❌ **No `copy_from_user`, no pointer validation, no SMAP.** Same exclusion as Step 1, same
  reason: the string pointer crosses into the kernel raw and is trusted, because there is still
  only ever one hand-written, kernel-authored blob. Phase 5 owns real validation.
- ❌ **No SMP-safe per-CPU array.** One static instance of whatever per-CPU state this Step needs
  is fine — see Decision 4.

---

## 🧠 The mental model, before any code

### Two genuinely different entry mechanisms, wearing the same exit

`svc #0` on AArch64 is **a synchronous exception** — the exact same mechanism Step 1's fault
took, landing in the exact same vector slot (**8**, Lower EL AArch64 Synchronous) that a trapped
`mrs` already landed in during Step 1's C2. `aarch64_dispatch()` already runs on every trap from
that slot; this Step adds one more `esr_ec` check next to the page-fault ones already there.
Genuinely small — this is the "nearly free" side Chapter 2 promises.

`syscall` on x86_64 is **not an exception at all**. It doesn't go through the IDT, doesn't consult
the TSS, doesn't push anything onto any stack. It reads `IA32_STAR`/`IA32_LSTAR`/`IA32_FMASK`,
puts the return address in `rcx` and `rflags` in `r11`, and jumps — still on the **user's** stack,
still with no register free and no trusted stack to work from. Building a `struct trap_frame` by
hand, from a standing start, with nowhere to put anything down first, is the actual difficulty
here — not the syscall dispatch logic itself, which is trivial once a frame exists.

**Both paths, once a frame exists, end the same way this project already knows**: fill in a
return value, let the existing exit path (`sched_on_trap_exit()` → the trap stub → `eret`/`sysretq`)
hand control back to userland. That's why A3 below recreates `struct trap_frame` by hand instead
of inventing a lighter one — it's what lets a task blocked mid-syscall still get preempted by the
next timer tick with zero special-casing in `sched.c`.

---

## ⚖️ Decisions to make before writing anything

### 1. Syscall numbers and the dispatch table

Per [`SYSCALL_DESIGN.md`](SYSCALL_DESIGN.md) §2: Linux numbers, not invented ones. Only `write`
is required, but build the table shape Phase 5 will grow into, not a one-off:

```c
// kernel/include/asm/unistd.h
#ifdef __aarch64__
#define SYS_write 64
#else
#define SYS_write 1
#endif
#define NR_SYSCALLS 512   // generous headroom; Phase 5 grows the table, not this constant
```

```c
// kernel/syscall/syscall.c
typedef int64_t (*syscall_handler_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

static syscall_handler_t syscall_table[NR_SYSCALLS] = {
    [SYS_write] = (syscall_handler_t) sys_write,
};

int64_t syscall_dispatch(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    if (num >= NR_SYSCALLS || !syscall_table[num]) return -1; // -ENOSYS, once errno.h exists
    return syscall_table[num](a0, a1, a2, a3, a4, a5);
}
```

`sys_write()`'s real signature is `(int fd, const void *buf, uint64_t count)` — three arguments,
not six. The cast through `syscall_handler_t` works because of how the SysV/AAPCS64 calling
conventions place the first three integer/pointer args, but it's worth being deliberate that
you're relying on that rather than discovering it works by accident.

**New directory, new problem**: `kernel/Makefile`'s build line only searches
`core mm drivers fs proc` for `.c` files (`$(shell find core mm drivers fs proc -name '*.c' ...)`).
A `kernel/syscall/` directory won't be picked up. Add `syscall` to that list, or put
`syscall.c` under `core/` instead — either is fine, just don't discover this the hard way via a
missing-symbol link error.

### 2. What does `fd 1` actually reach?

The real gap found while writing this document: `open_files[]` (`fs/vfs/file.c`) starts with
every slot closed, and nothing pre-opens fd 0/1/2 against anything. `write(1, ...)` against
today's `sys_write()` would just return `-1` (bad fd) — not because privilege failed, but because
stdout doesn't exist yet.

**Recommendation: special-case fd 1/2 in `syscall_dispatch()` (or a thin wrapper immediately
around it), not inside `sys_write()` itself.**

```c
int64_t sys_write_console_or_vfs(int fd, const void *buf, uint64_t count) {
    if (fd == 1 || fd == 2) {
        const char *chars = buf;
        for (uint64_t i = 0; i < count; i++) console_putc(chars[i]);
        return (int64_t) count;
    }
    return sys_write(fd, buf, count);
}
```

Wire this (not raw `sys_write`) into the dispatch table for `SYS_write`. Reasoning: it leaves
`fs/vfs/file.c` — already exercised by `tar_load()` during boot — completely untouched, and it's
an explicit, visible stopgap that Phase 6's real `/dev/console` VFS node deletes outright rather
than something baked into the file-descriptor table's own logic.

### 3. Reuse `struct trap_frame`, don't invent a lighter one

Covered in the mental model above — the payoff is that `sched_on_trap_exit()` needs no changes
at all. A syscall-in-flight task is, to the scheduler, just a task sitting at a trap, same as any
other. This is also why AArch64's B1 below is so small: `svc` already produces a `struct
trap_frame` for free.

### 4. x86_64: `swapgs`, taken deliberately — [`PHASE4_PRIVILEGE.md`](PHASE4_PRIVILEGE.md) Chapter 4
already made this call: *"Finding the kernel stack on x86 | swapgs vs single-CPU global | swapgs
— same effort, survives SMP."* One static per-CPU struct today, sized for one CPU, is the
pattern to build even though only one instance exists:

```c
// arch/x86_64/percpu.h (new)
struct x86_64_percpu {
    uint64_t kernel_rsp; // what syscall_entry swaps in
    uint64_t user_rsp;   // stashed here while running kernel-side
};
extern struct x86_64_percpu x86_64_percpu0; // the only instance, for now
```

`IA32_KERNEL_GS_BASE` gets pointed at `&x86_64_percpu0`. **`arch_set_kernel_stack()` needs to
write `x86_64_percpu0.kernel_rsp` too, not just `idt_tss_set_rsp0()`** — the TSS's `rsp0` is only
ever consulted by *interrupt*-taken traps; `syscall` never looks at it. Forgetting this is the
single most likely silent bug in this Step: everything compiles, `#GP`/`#PF` still get a correct
kernel stack via the TSS, but a `syscall` from a freshly-switched-to task builds its frame on a
stale or zeroed stack.

### 5. The `sysret` erratum — ignore it, but say so

Chapter 4's call, carried forward: if `rcx` holds a non-canonical address, `sysretq` `#GP`s **in
ring 0, on the user stack** — a real problem for a kernel running untrusted binaries, not urgent
for one hand-fabricated blob. Comment it at the `sysretq` call site so it reads as a decision, not
an oversight.

---

## 🗺️ Suggested order

```
  C0 ── C1 ──┬── B1 ─────────────── B-verify (AArch64 end to end)
             └── A1 ── A2 ── A3 ──── A-verify (x86_64 end to end)
                                          │
                                          └── C2 ── C3
   │    │         │                             │    └─ bounds/adversarial checks
   │    │         │                             └─ the real end-to-end proof, both arches
   │    │         └─ MSRs ── percpu ── syscall_entry.S
   │    └─ fd 1/2 special case, still callable from ring 0
   └─ syscall numbers + dispatch table, callable from ring 0
```

Same discipline as Step 1: **C0 and C1 are entirely verifiable from ring 0**, by calling
`syscall_dispatch()` directly from `main.c` before any trap mechanism exists. Get the plumbing
right where a mistake is a `kprintf` away from obvious, then hand control to the two entry paths.

---

## 🧩 C0 — Syscall numbers and the dispatch table ⬜

**Goal:** `syscall_dispatch(SYS_write, ...)`, called directly from `main.c`, reaches `sys_write()`.
No trap involved yet.

Build `kernel/include/asm/unistd.h` and `kernel/syscall/syscall.c` per Decision 1. Fix the
Makefile's `find` line.

**Verify:** from `main.c`, in ring 0, call `syscall_dispatch(SYS_write, 1, (uint64_t)"C0 ok\n", 6, 0, 0, 0)`
directly (fd 1 won't resolve yet — that's C1) and confirm the return value and any error path
behave as expected. An out-of-range syscall number should return the not-implemented value, not
crash.

---

## 🧩 C1 — `fd 1`/`fd 2` reach the console ⬜

**Goal:** the same direct, ring-0 call now actually prints.

Wire Decision 2's wrapper into the dispatch table in place of raw `sys_write`.

**Verify:** re-run C0's direct call. `"C0 ok"` (or similar) should now genuinely appear on the
console, still without any privilege transition ever happening. This is the last thing checkable
before handing control to code that can't be debugged by printing.

---

## 🅱️ B1 — AArch64: `svc` dispatch ⬜

**Goal:** `write(1, ...)` from EL0 works, end to end.

In `aarch64_dispatch()` (`arch/aarch64/exceptions.c`), right beside the existing page-fault `esr_ec`
checks:

```c
uint32_t esr_ec = (frame->esr >> 26) & 0x3F;

if (esr_ec == 0x15) { // SVC instruction execution in AArch64 state
    int64_t ret = syscall_dispatch(frame->x[8], frame->x[0], frame->x[1], frame->x[2],
                                    frame->x[3], frame->x[4], frame->x[5]);
    frame->x[0] = (uint64_t) ret;
    return;
}
```

Number in `x8`, args in `x0`-`x5`, return value written back into `x0` — the exit path (already
correct since Phase 3) restores it, and userland sees its syscall return like any function call.
No changes to `vectors.S`, no new vector slot: `svc` from EL0 already lands at slot 8, the exact
slot a trapped `mrs` used in Step 1.

**Verify:** extend the Step 1 AArch64 blob (see C2) and boot. This is genuinely the whole AArch64
side of this Step — if C0/C1 are right, this Part is a handful of lines.

---

## 🅰️ A1 — x86_64: the five MSRs ⬜

**Goal:** `syscall` is enabled and configured to land somewhere. Verifiable without ever executing
it.

In `arch_init()` (`arch/x86_64/arch.c`), after checking `IA32_EFER.SCE` isn't already set:

| MSR | Value |
|---|---|
| `IA32_EFER` | set bit 0 (SCE) |
| `IA32_LSTAR` | address of `syscall_entry` (A3) |
| `IA32_STAR` | `STAR[63:48] = X86_64_STAR_USER_BASE` (`0x28`, already defined from Step 1 A1), `STAR[47:32] = X86_64_SELECTOR_KERNEL_CODE` (`0x08`) |
| `IA32_FMASK` | at minimum `X86_64_RFLAGS_IF` — **interrupts must be masked on entry**, or a syscall can be interrupted before it has a kernel stack |
| `IA32_KERNEL_GS_BASE` | `&x86_64_percpu0` (Decision 4) |

You'll need a `wrmsr` helper — `lapic.c:35` already has `rdmsr` as a `static` file-local helper;
either duplicate the pattern in `arch.c` or promote both to a shared header. Both `rdmsr`/`wrmsr`
take the MSR index in `ecx`; `wrmsr` takes the value split `edx:eax`.

**Verify:** `rdmsr` each one back immediately after writing it and `kprintf` them. All five should
read back exactly what was written — this is checkable entirely at rest, in ring 0, before `A3`
gives `syscall` anywhere real to jump to.

---

## 🅰️ A2 — x86_64: per-CPU state and `arch_set_kernel_stack()` ⬜

**Goal:** something for `swapgs` to find, and it stays correct across context switches.

Add `x86_64_percpu0` per Decision 4's sketch. Extend `arch_set_kernel_stack()`
(`arch/x86_64/arch.c:146`) — currently just `idt_tss_set_rsp0(stack_top)` — to also write
`x86_64_percpu0.kernel_rsp = (uint64_t) stack_top`.

**Verify:** after a context switch (any existing kernel-thread switch already exercises this),
`kprintf` `x86_64_percpu0.kernel_rsp` and confirm it matches the newly-scheduled task's kernel
stack top, same value `tss.rsp0` would show. Two call sites, one value, cheap to cross-check.

---

## 🅰️ A3 — x86_64: `syscall_entry.S` ⬜

**Goal:** the fiddly one. A hand-built `struct trap_frame`, from a standing start, on the user's
stack, with no free register.

New file, `arch/x86_64/syscall_entry.S`. The shape, following
[`PHASE4_PRIVILEGE.md`](PHASE4_PRIVILEGE.md) Chapter 2's sketch and `isr.S`'s existing GPR-push
order (`isr.S:69-83`, so `sched_on_trap_exit()` and every dump routine can treat this frame
identically to an interrupt-taken one):

```asm
.global syscall_entry
syscall_entry:
    swapgs
    movq %rsp, %gs:8            /* x86_64_percpu0.user_rsp — stash the user stack */
    movq %gs:0, %rsp            /* x86_64_percpu0.kernel_rsp — switch to this task's kernel stack */

    /* build a struct trap_frame by hand: nothing pushed it for us */
    pushq $X86_64_SELECTOR_USER_DATA   /* ss */
    pushq %gs:8                        /* rsp (the stashed user rsp) */
    pushq %r11                         /* rflags, saved here by `syscall` itself */
    pushq $X86_64_SELECTOR_USER_CODE   /* cs */
    pushq %rcx                         /* rip, saved here by `syscall` itself */
    pushq $0                           /* error_code slot, unused for a syscall */
    pushq $SYSCALL_INT_NO_SENTINEL     /* int_no slot: not a real vector, just legible in a dump */

    /* now push GPRs in isr.S's exact order so the frame layout matches */
    pushq %rax   /* still holds the syscall number here */
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rdi
    call x86_64_syscall_handler   /* new C function, mirrors x86_64_exception_handler's job */
    movq %rax, %rsp               /* restore from whatever frame the handler returned */

    /* pop GPRs back, same order as isr.S */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax                     /* return value, per the syscall ABI's use of rax */

    addq $16, %rsp                /* drop int_no/error_code, same as isr.S */
    popq %rcx                     /* rip, back into rcx for sysretq */
    addq $8, %rsp                 /* drop cs, sysretq computes it */
    popq %r11                     /* rflags, back into r11 for sysretq */
    popq %rsp                     /* the (possibly new, if switched) task's user rsp — must be last */

    swapgs
    sysretq
```

A new C function, `x86_64_syscall_handler(struct trap_frame *frame)`, does what
`x86_64_exception_handler()` does for interrupts — pull `rax`/args out of the frame, call
`syscall_dispatch()`, write the return value into the frame's `rax` slot, and return `frame` (or
whatever `sched_on_trap_exit()` returns, exactly like the interrupt path) so the pop sequence
above restores the *possibly different* task the scheduler picked. **Deliberately not merged into
`x86_64_exception_handler()`** — a syscall didn't arrive through the IDT and has no interrupt
number; keeping the two dispatchers separate is what lets `int_no` stay meaningful in every other
crash dump.

`popq %rsp` last, right before `swapgs`/`sysretq`, is the step most likely to go wrong: it has to
be the very last register restored, because it's what makes every earlier `popq` operate on the
*kernel* stack while this one switches back to the (possibly new) task's *user* stack, mirroring
`ldp x0, x1, [sp], #TRAP_FRAME_SIZE` releasing the AArch64 frame in one instruction on the way
out.

**Verify:** this Part is genuinely not testable at rest — the first meaningful signal is A-verify
below, and the failure modes table has a symptom for nearly everything that can go wrong.

---

## 🧩 C2 — The real end-to-end proof ⬜

**Goal:** extend Step 1's blob on both architectures. `write` prints, then the blob deliberately
faults exactly like Step 1 did.

```
    x86_64:                          AArch64:
    mov rax, 1        ; SYS_write    mov x8, #64          ; SYS_write
    mov rdi, 1        ; fd           mov x0, #1           ; fd
    lea rsi, [rip+msg]; buf          adr x1, msg          ; buf
    mov rdx, 18       ; count        mov x2, #18          ; count
    syscall                          svc #0
    hlt                              mrs x0, sctlr_el1
    jmp .                            b .
msg: .ascii "hello from ring 3\n"
```

Assemble and verify the bytes with the toolchain, same discipline as every blob in Step 1 — don't
trust the listing above, including the `lea`/`adr` encodings, which are exactly the kind of thing
that looks obviously right and isn't.

**Expected:** `hello from ring 3` on the console, **then** the same unhandled-exception dump Step
1 produced (`#GP` / trapped `mrs`), proving the machine survived the syscall cleanly enough to
keep running before the deliberate fault. If the string never appears, the syscall path is broken
somewhere before dispatch; if it appears but the fault dump looks wrong (`CS`/`SPSR` no longer
show ring 3 / EL0t), the return-to-userland half of the syscall path corrupted the frame.

---

## 🧩 C3 — Bounds and adversarial checks ⬜

**Goal:** a couple of cheap experiments, same spirit as Step 1's C3.

| Experiment | Blob | Expected |
|---|---|---|
| **Unimplemented syscall number** | `x8`/`rax` = some large, unused number | `syscall_dispatch()`'s bounds check returns the not-implemented value; no crash, no garbage function-pointer call |
| **`write` to an fd that isn't 1/2 and was never opened** | `fd = 99` | `sys_write()`'s existing `fd >= MAX_OPEN_FILES` check declines cleanly; return value is negative, blob keeps running |

Both should be checkable by extending C2's blob temporarily, same "change it, observe, put it
back" pattern as Step 1.

---

## 🔬 Debugging tools worth knowing before you need them

- **`-d int` will *not* show `syscall` the way it shows exceptions** — `syscall` isn't an
  exception, so it never appears in QEMU's interrupt trace. If the blob hangs or resets on the
  x86_64 side, `-d int` will only tell you what happened *before* or *after* the syscall, not
  during it. `info registers` in the monitor, mid-hang, is the tool here instead — checking `RIP`
  tells you whether execution is still inside `syscall_entry` (and roughly where) or never got
  there at all.
- **A wrong `IA32_STAR` produces a `#GP` *on the return*, at an address that looks like perfectly
  good user code** — `PHASE4_PRIVILEGE.md` Chapter 5 already names this trap. If C2's fault dump
  shows the wrong `RIP` entirely (not the deliberate second-instruction fault, but something
  earlier or nonsensical), suspect `STAR` before suspecting the blob.
- **The AArch64 side is debuggable by printf almost the whole way** — B1's addition runs in
  ordinary C, in an already-working dispatch function. If it's not working, add a `kprintf` right
  inside the `esr_ec == 0x15` branch before assuming anything about registers.

---

## 🕳️ Failure modes, collected

- **Triple fault / instant reset on the very first `syscall`.** `x86_64_percpu0.kernel_rsp` was
  never set (A2 not wired into `arch_set_kernel_stack()`, or `arch_set_kernel_stack()` was never
  called for this task) — `swapgs` finds garbage, `movq %gs:0, %rsp` sets `rsp` to garbage, and
  the very first `pushq` after that faults with no valid stack to build a fault frame on either.
- **Runs, but corrupts an unrelated kernel variable shortly after.** Same shape as Step 1's
  task-0-stack hazard, but for `kernel_rsp` instead of `tss.rsp0` — if the task making the syscall
  is task 0 (Limine's boot stack), its `kernel_rsp` was never a real allocated stack. Same fix:
  only `task_create_user()`-made tasks should ever reach a syscall in testing.
- **`#GP` immediately on `sysretq`, `RIP` looks like valid user code.** `IA32_STAR`'s layout is
  off by a slot — recheck against A1's table and Step 1 A1's GDT layout (`0x28`/`0x30`/`0x38`).
- **Return value in userland is garbage / wrong register.** Check the exact GPR pop order in
  `syscall_entry.S` matches the push order — a single swapped `popq` two lines apart silently
  swaps two registers' values on return, and `rax` specifically must be the *last* GPR popped
  (right before the frame-teardown sequence), since it's what the syscall ABI defines as the
  return-value register.
- **AArch64: syscall runs but return value never reaches userland.** Check `frame->x[0]` is
  written *before* `return` in `aarch64_dispatch()`'s new branch, not after — the existing exit
  path only restores whatever is in the frame at the point control leaves `aarch64_dispatch()`.
- **AArch64: works once, then the second syscall from the same task corrupts something.** Almost
  certainly a bug in `syscall_dispatch()` or `sys_write()` itself (state left over from the first
  call), not the entry path — B1's addition is stateless per call, so a bug that only shows up on
  repetition points elsewhere.

---

## 📁 Files touched

- ⬜ `kernel/include/asm/unistd.h` — new, `SYS_write`, `NR_SYSCALLS`
- ⬜ `kernel/syscall/syscall.c` — new, `syscall_dispatch()`, the table, the fd 1/2 wrapper
- ⬜ `kernel/Makefile` — add `syscall` to the `find` line (or relocate the file under `core/`)
- ⬜ `kernel/arch/aarch64/exceptions.c` — one new `esr_ec == 0x15` branch in `aarch64_dispatch()`
- ⬜ `kernel/arch/x86_64/arch.c` — the five MSRs in `arch_init()`; `arch_set_kernel_stack()`
  extended to also write `x86_64_percpu0.kernel_rsp`; `wrmsr` helper
- ⬜ `kernel/arch/x86_64/percpu.h` — new, `struct x86_64_percpu`, the one static instance
- ⬜ `kernel/arch/x86_64/syscall_entry.S` — new, the entry/exit trampoline
- ⬜ `kernel/arch/x86_64/idt.c` / `.h` — a new `x86_64_syscall_handler()` C function (can live
  beside `x86_64_exception_handler()`, but stays a separate function — see A3)
- ⬜ `kernel/core/main.c` — extend both blobs with the `write` call ahead of the existing fault

Deliberately **not** touched: `vectors.S` (AArch64's entry path is entirely existing machinery —
see the mental model section), `isr.S` (the IDT path is untouched; `syscall` bypasses it
completely), `fs/vfs/file.c` (Decision 2's whole point), `sched.c` (a syscall is just another trap
as far as the scheduler is concerned).

---

## 🪜 Verification

Same story as Step 1: a privilege transition and a fast syscall entry aren't assertable from
`core/test/`. C0/C1 *are* testable at rest, called directly from `main.c` in ring 0 — do that
before touching either entry path, exactly like Step 1's C0/C1/A1/B1 verify-before-scheduling
discipline.

| Check | Expected |
|---|---|
| C0, ring 0 | `syscall_dispatch(SYS_write, ...)` reaches `sys_write()`; out-of-range number declines cleanly |
| C1, ring 0 | the same call, now targeting fd 1, actually reaches the console |
| A1 | all five MSRs read back what was written, before `syscall` ever executes |
| A2 | `x86_64_percpu0.kernel_rsp` tracks `tss.rsp0` across a context switch |
| B1 | AArch64 blob's `svc #0` prints and returns control to the next instruction |
| **C2, both arches** | **`hello from ring 3` on the console, then the same fault dump Step 1 produced** |
| C3 | unimplemented syscall number and a bad fd both decline without crashing |

Phase 4's stated goal — *a program the kernel does not trust calls `write` and the string appears
on the console* — is met at C2. What's left open afterward, worth carrying into Phase 5 rather
than solving here: the fd 1/2 special case (Decision 2) is a stopgap for a real `/dev/console`;
`-errno` returns don't exist yet; and every user task, syscall or not, still ends in a register
dump, because there is still no `exit`.
