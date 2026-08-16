# Working Document: Phase 5 Step 1 — The ELF64 Loader and a Freestanding `/bin/init` [STATUS: IN PROGRESS 🚧 — D0 done]

> [!NOTE]
> **Phase 5 Step 1**, from [`PHASE5_LOADING.md`](PHASE5_LOADING.md) Chapter 3, expanded into
> individually verifiable Parts. Read Chapters 1-2 there for the *why* — the `PT_LOAD` algorithm,
> the HHDM copy trick, the initial-stack layout — this document is the *how*, in order. Chapter 2
> (per-process fd tables, `copy_from_user`, `-errno`) is Step 2, not here.

> [!NOTE]
> Mentor-mode reminder (see the note at the top of [`../README.md`](../README.md)): this is a
> design reference to code from by hand, not code to paste in. Signatures, struct fields and
> constants are given exactly; the bodies are yours.

---

## 🎯 What "done" looks like

> `/bin/init` — a real ELF64 binary, compiled by this repo's own `zig cc`, sitting in the initrd
> — is parsed, its `PT_LOAD` segments mapped into a **fresh address space**, its `.bss` zeroed,
> handed a real `argc`/`argv`/`envp`/`auxv` stack, and scheduled. It runs at ring 3 / EL0 and
> calls `write(1, ..., ...)` — the one syscall Phase 4 wired — and the string appears on the
> console.

Compare against Phase 4 Step 1's hand-assembled blob mapped into the *kernel* context: every
"deliberately not in this step" item on that document's list except syscalls is what this Step
adds. The blob becomes a file; the shared context becomes a private one.

**What is explicitly *not* in this step:**

- ❌ **No new syscalls.** Only `write` is reachable, exactly as Step 2 left it. `open`/`read`/
  `close`/`getpid`, per-process fd tables, and `-errno` are all Phase 5 **Step 2**.
- ❌ **No `copy_from_user`.** The loader itself never trusts anything from the *file* on faith
  (that's what validation is for), but nothing here validates a *userland-supplied pointer* —
  there isn't one yet, since `write`'s buffer still comes from `init`'s own known-good stack.
- ❌ **No dynamic linking, no PIE.** Static, non-PIE `ET_EXEC` only. `PT_INTERP`, `PT_DYNAMIC`
  and relocations are rejected, not silently ignored.
- ❌ **No task exit.** `init` ends the same way Phase 4's blob did — it runs off the end of
  whatever it does, faults or halts, and the kernel panics. Phase 7's problem.

---

## 🧠 The one new idea C0 exists to fix

Phase 4 Step 1 deliberately mapped its blob into `vmm_kernel_context` to keep that step to a
single new idea. The cost of that shortcut is due now: **`task->ctx` has never been set, and
`vmm_switch_context()` has never been called outside of `vmm_init()`.** Confirmed by reading the
tree — `task_create()` has `task->ctx = vmm_create_context()` commented out
(`src/proc/task.c:35`), and `sched_on_trap_exit()` (`src/proc/sched.c`) swaps `frame` and
`kernel_stack_top` on every switch but never touches `vmm_current_context`.

That was invisible until now because every task so far has run entirely in the upper half
(kernel code) or borrowed the kernel's own root table (Phase 4's blob). The instant the loader
maps `init`'s segments into a **second** root table, whichever root the CPU actually has loaded
matters for the first time — CR3 / TTBR0 has pointed at `vmm_kernel_context->root_phys` since
boot and nothing has ever changed it.

---

## ⚖️ Decisions to make before writing anything

### 1. Every task gets a `ctx` now, not just user ones

**Recommendation: `task->ctx` is never `NULL` after `task_create()`/`task_create_user()`
returns.** Kernel threads get `vmm_kernel_context` (skip the `vmm_create_context()` call, just
assign it); user tasks get a freshly created one. `sched_on_trap_exit()` then does exactly one
new thing, unconditionally: `vmm_switch_context(next->ctx)` before returning `next->frame`.

The alternative — `NULL` means "leave whatever's loaded alone" — works today because only one
user task will ever exist, but it's a lazy-TLB optimization Linux earns deliberately, not a
default worth reaching for on task 3 of a hobby kernel. A CR3 write on every switch is
free next to fabricating a frame.

### 2. Where does `init`'s segments and stack live?

Same 1 GiB landmark Phase 4 chose, but now in `task->ctx` instead of `vmm_kernel_context` — the
constant doesn't change, only which root table it's a leaf of.

```
0x40000000  PT_LOAD segments, per the ELF's own p_vaddr — position depends on the linker,
            not a kernel constant. Static/non-PIE means these are fixed at link time.
0x7FFF_xxxx region (or any address below VMM_ADDR_SPLIT, above the segments) for the stack —
            recommendation: also fixed at 1 GiB + some headroom, e.g. USER_STACK_TOP =
            0x0000000040800000, leaving 8 MiB between segment top and stack bottom unmapped
            as an accidental-growth guard.
```

### 3. One `init.c`, or one per architecture? — 🗄️ superseded, see below

🗄️ **No longer as described here.** This decision assumed `limine.conf` would keep shipping a
single, shared `module_path:` for both boot entries, forcing arch-suffixed filenames inside one
shared tar so one architecture's `init` couldn't silently overwrite the other's. That constraint
is gone: the build system that came out of D0 gives each architecture its **own** `initrd`
archive (`initrd/bin/$(ARCH)/initrd`), and `limine.conf`'s two entries each point at their own
`boot():/boot/$(ARCH)/{kernel,initrd}`. There's nothing to disambiguate inside a single tar
anymore, so `main.c`'s `init_path` is just `"/bin/init"`, unconditionally — the
`#ifdef __x86_64__` split this decision recommended never ended up needed.

*(Original reasoning, kept for the trail: `limine.conf` used to carry a single shared
`module_path: boot():/boot/initrd.tar` for both boot entries, which was the constraint this
decision was working around by suggesting `initrd/bin/init-x86_64` / `initrd/bin/init-aarch64`
inside one tar. The "alternative" this section dismissed as "a bigger change for the same
result" — one `initrd.tar` per architecture — is the one that actually shipped.)*

### 4. `e_machine` validation, per architecture

`EM_X86_64` is `62`, `EM_AARCH64` is `183` — reject anything else outright. Rejecting the *other*
architecture's binary with `kprintf("ELF", "wrong e_machine: got %u, want %u\n", ...)` beats
letting it through and debugging a fault in garbage-decoded instructions, same reasoning as
[`PHASE5_LOADING.md`](PHASE5_LOADING.md) Chapter 1 already gives for magic/class.

---

## 🗺️ Suggested order

```
  D0 ──┬── C1 ── C2 ──┐
       │              ├── C4
  C0 ──┴────── C3 ────┘

  D0 ── a real ELF to test C1 against (build system + init.c)
  C0 ── every task owns a ctx; the scheduler finally switches it
  C1 ── parse + validate Elf64_Ehdr / Elf64_Phdr
  C2 ── map PT_LOAD segments through the HHDM, zero .bss
  C3 ── build argc/argv/envp/auxv on the new stack, through the HHDM too
  C4 ── wire it together: main.c loads and schedules /bin/init
```

D0 and C0 are independent of each other and worth doing in either order — D0 because nothing in
C1 is checkable without a real binary to point it at, C0 because it's a correctness prerequisite
that doesn't touch ELF at all and can be proven with tools that already exist (`task_dump_all()`,
a hand-mapped page, same as Phase 4 Step 1 C0's verify step). C2 and C3 both depend on C0 (a
`ctx` to map into) and can be built in either order once it lands.

**Status: D0 done, C0 next.** C1-C4 are all unstarted.

---

## 🧩 D0 — A real, freestanding ELF64 binary to load ✅ DONE (2026-08-15)

**Goal, as built:** two static, non-PIE ELF64 binaries, `user/bin/x86_64/init` and
`user/bin/aarch64/init`, built by real inline-asm `write` syscall wrappers matching this
kernel's own trap-entry ABI (`syscall` on x86_64, `svc #0` on AArch64), packed into
`initrd/bin/$(ARCH)/initrd` — nothing in the kernel changed yet.

```c
// user/src/init/init.c — no libc, no CRT: _start IS the entry point
#ifdef __x86_64__
static inline long sys_write(long fd, const void *buf, long len) {
    long ret;
    asm volatile ("syscall" : "=a"(ret) : "a"(1), "D"(fd), "S"(buf), "d"(len) : "rcx", "r11", "memory");
    return ret;
}
#else
static inline long sys_write(long fd, const void *buf, long len) {
    register long x8 asm("x8") = 64;
    register long x0 asm("x0") = fd, x1 asm("x1") = (long) buf, x2 asm("x2") = len;
    asm volatile ("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
}
#endif

void _start(void) {
    sys_write(1, "hello from /bin/init\n", 22);
    for (;;) {}
}
```

**What actually shipped, different from the plan above:** rather than two Makefile rules bolted
onto the top-level `Makefile`, writing `init.c` turned into restructuring the whole build system —
`kernel/`, `user/`, and `initrd/` are now three sibling, ARCH-parametrized subprojects (each with
its own `Makefile` + `config.$(ARCH).mk`). Every new user program
(`user/src/<name>/<name>.c`) is picked up automatically — `user/Makefile` discovers it via
`$(wildcard src/*/)` and generates its build rule, `initrd/Makefile` packs it via
`$(wildcard ../user/bin/$(ARCH)/*)` — no per-program edits needed anywhere. See decision 3 below
(now superseded) for how that changed the `initrd`-per-architecture question.

One toolchain gotcha hit along the way, worth keeping for the next binary: `-Wl,-Ttext=0x40000000`
(this doc's original recommendation) is rejected by this `zig cc`'s bundled linker
(`unsupported linker arg: -Ttext`) — `-Wl,--image-base=0x40000000` is the form that actually
works, and is what `user/config.*.mk`'s `LDFLAGS` uses.

**Verify — done:**
```
$ readelf -h user/bin/x86_64/init | grep -i "Type\|Machine\|Entry"
  Type:                              EXEC (Executable file)
  Machine:                           Advanced Micro Devices X86-64
  Entry point address:               0x400011f0

$ readelf -h user/bin/aarch64/init | grep -i "Type\|Machine\|Entry"
  Type:                              EXEC (Executable file)
  Machine:                           AArch64
  Entry point address:               0x400101dc
```
Both `Type: EXEC`, correct `Machine` each, entry near the linked `0x40000000`, two `LOAD`
segments each (a `.text`/`.rodata` split — no `.bss` to speak of yet, matches expectations for
this ~15-line program). Same discipline
[`PHASE4_STEP1_RING3_EL0.md`](archive/PHASE4_STEP1_RING3_EL0.md) used on hand-assembled bytes —
verified against the toolchain's own output, never a listing.

---

## 🧩 C0 — Every task owns a context, and the scheduler finally switches it

**Goal:** `task->ctx` is populated for every task, kernel threads included, and
`sched_on_trap_exit()` calls `vmm_switch_context()` on every switch. **No user code involved —
this is provable entirely with the existing kernel threads.**

```c
// src/proc/task.c — task_create() (kernel thread path)
task->ctx = vmm_kernel_context;   // was: commented-out vmm_create_context()

// src/proc/task.c — task_create_user()
task->ctx = vmm_create_context();
if (!task->ctx) { kstack_free(task->kernel_stack_base); kfree(task); return NULL; }
```

```c
// src/proc/sched.c — sched_on_trap_exit(), right where kernel_stack is switched
arch_set_kernel_stack(next->kernel_stack_top);
vmm_switch_context(next->ctx);
current = next;
```

### Why this is safe for T1/T2 today

`vmm_kernel_context`'s lower half is empty — nothing has ever mapped a low address into it
except Phase 4's now-commented-out blob. Switching CR3/TTBR0 to the *same* physical root on
every kernel-thread switch is a correct no-op: `arch_vmm_set_user_root()` writes a value that's
already loaded. The only thing that changes observably is a write that used to never happen now
happening every tick — worth confirming the boot log and switch counters are unaffected.

**Verify, without touching ELF:** re-run Phase 3/4's boot scenario — T1/T2 interleaving,
`task_dump_all()` counters near-equal — and confirm nothing regresses. Then, temporarily, give
one kernel thread `vmm_create_context()`'s result instead of `vmm_kernel_context` and confirm
`vmm_current_context` (printed) actually changes across a switch — proof the call is wired, not
just present in the source.

---

## 🧩 C1 — Parse and validate the ELF header and program headers

**Goal:** `elf_load()` can read `Elf64_Ehdr` and walk `Elf64_Phdr[]` out of a file, rejecting
anything that isn't a static, non-PIE, this-architecture ET_EXEC — proven against D0's real
binaries, no mapping yet.

```c
// include/proc/elf.h
struct elf_load_result {
    uint64_t entry;         // e_entry — feed straight to arch_task_init_user_frame()
};

// Parse and map path's PT_LOAD segments into ctx. ctx must already exist
// (vmm_create_context()) and must not yet be the active context.
int elf_load(const char *path, struct vmm_context *ctx, struct elf_load_result *out);
```

Read the header with `vfs_lookup()` + `node->ops->read()` directly — this is kernel-internal
loading, not a syscall, so it has no reason to go through `sys_open`/`sys_read`/the fd table.

Validation, in order, each with its own `kprintf` reason:

- `e_ident[EI_MAG0..3] == "\x7fELF"`, `e_ident[EI_CLASS] == ELFCLASS64 (2)`.
- `e_type == ET_EXEC (2)` — reject `ET_DYN` (PIE) explicitly rather than mis-loading it as if
  it were fixed-address.
- `e_machine` matches this build's architecture (decision 4 above).
- `e_phoff`/`e_phnum`/`e_phentsize` are sane (`e_phentsize == sizeof(struct elf64_phdr)`)
  before trusting them to index into the file.

**Verify:** point `elf_load()` at D0's real binaries — logging the parsed fields instead of
mapping anything yet — and confirm they match `readelf`'s output exactly. Then deliberately
truncate/corrupt a copy (`dd` a few bytes) and confirm each validation branch is individually
reachable, not just the first one.

---

## 🧩 C2 — Map `PT_LOAD` segments through the HHDM, zero `.bss`

**Goal:** every `PT_LOAD` segment lands in `ctx` at its own `p_vaddr`, content copied, `.bss`
zeroed, permissions applied last — the algorithm [`PHASE5_LOADING.md`](PHASE5_LOADING.md)
Chapter 1 already specifies. This Part is "go implement that", plus the partial-page case it
flags as needing care.

The loop, per `PT_LOAD` header:

- `vaddr_lo = p_vaddr & ~(PAGE_SIZE-1)`, `vaddr_hi = ROUND_UP(p_vaddr + p_memsz, PAGE_SIZE)`.
- For each page in `[vaddr_lo, vaddr_hi)`: if `vmm_virt_to_phys(ctx, page)` is already
  non-zero (the shared-tail-page case from Chapter 1), reuse that physical page; otherwise
  `pmm_alloc(1)` a fresh one and `vmm_map_page(ctx, page, phys, VMM_USER | VMM_WRITABLE)` —
  **writable and without permissions finalized yet**, exactly like Phase 4 Step 1 C0's blob
  pages, for the same reason: content has to land before read-only is enforced.
- `memcpy` `p_filesz` bytes from the file into `pmm_phys_to_virt(phys) + (p_vaddr - page)`,
  reading through the *same* `node->ops->read()` C1 already proved works.
- `memset` the `p_memsz - p_filesz` tail to zero — this is `.bss`, and it is *usually already
  zero* fresh off `pmm_alloc()`, which is exactly the trap Chapter 5 names: it works by
  accident until a page gets reused with stale content.
- After every segment's pages are filled: a second pass re-`vmm_map_page()`s each leaf with
  its real `p_flags` translated to `VMM_USER | (VMM_WRITABLE if PF_W) | (0 if PF_X else
  VMM_NO_EXECUTE)` — this is what makes a read-only `.text` genuinely read-only from the
  process's very first instruction.

**Verify:** after loading (before scheduling anything), from ring 0, `vmm_virt_to_phys(ctx, ...)`
each mapped page and `memcpy` it back out through the HHDM alias for comparison against the file
— byte-for-byte for the `p_filesz` region, all-zero for the `.bss` tail. This is checkable
entirely from kernel code, same as Phase 4 Step 1 C0's readback.

---

## 🧩 C3 — The initial user stack: `argc`, `argv`, `envp`, `auxv`

**Goal:** a stack built at `USER_STACK_TOP` in `ctx`, laid out per
[`PHASE5_LOADING.md`](PHASE5_LOADING.md) Chapter 1's diagram, 16-byte aligned, terminated with
`AT_NULL` — `init` needs nothing more, but the table shape is built for Phase 9's real `auxv`
from day one per that document's recommendation.

This is the Part the design doc doesn't spell out but C2 already establishes the pattern for:
**the stack is also memory in a context that isn't active**, so it gets the identical HHDM
treatment — `pmm_alloc()` the stack page(s), write the layout through
`pmm_phys_to_virt(phys)`, map it into `ctx` last.

```c
// include/proc/elf.h
// Writes argc/argv/envp/auxv onto a freshly allocated, mapped stack page in ctx.
// Returns the resulting stack pointer (16-byte aligned) to hand to
// arch_task_init_user_frame(), or 0 on failure.
uint64_t elf_build_user_stack(struct vmm_context *ctx, uint64_t stack_top,
                               int argc, char *const argv[], char *const envp[]);
```

Build order (high addresses to low, since the stack grows down and pointers must reference
already-placed string data):

1. Copy `argv`/`envp` string bytes onto the stack, tightly packed, remembering each one's
   resulting address.
2. Lay down `AT_NULL` (two `uint64_t` zeros).
3. Lay down `envp` pointers, high to low, terminated by `NULL`.
4. Lay down `argv` pointers, same shape, terminated by `NULL`.
5. Lay down `argc` as a single `uint64_t`.
6. Round the final pointer **down** to 16-byte alignment before returning it — rounding up
   would place `argc` outside what was written.

For `init` specifically: `argc = 0`, `argv = {NULL}`, `envp = {NULL}` is a legitimate call —
worth building the general function anyway, since Phase 6's `psh` needs real arguments and
retrofitting alignment logic under time pressure is how Phase 4 Step 1's frame bugs happened.

**Verify:** print the built layout's addresses and confirm: final `rsp % 16 == 0`, walking
`argv[0..argc-1]` and `envp[]` from the written pointers lands on the expected strings, and the
`AT_NULL` terminator is exactly where `auxv` parsing would stop looking.

---

## 🧩 C4 — Wire it together: `main.c` loads and schedules `/bin/init`

**Goal:** the throwaway blob-scheduling code in `main.c` (currently block-commented, per
[[project_phase4_step1_status]]) is replaced by real loading — this is where Step 1's stated
acceptance criterion happens.

```c
// src/core/main.c, replacing the commented-out blob block
struct vmm_context *init_ctx = vmm_create_context();
if (!init_ctx) kpanic("vmm_create_context for init failed");

struct elf_load_result elf;
if (elf_load(init_path, init_ctx, &elf) != 0) kpanic("elf_load failed");

uint64_t sp = elf_build_user_stack(init_ctx, USER_STACK_TOP, 0, NULL, NULL);
if (!sp) kpanic("elf_build_user_stack failed");

struct task *init_task = task_create_user("init", elf.entry, sp);
if (!init_task) kpanic("task_create_user failed");
init_task->ctx = init_ctx;                 // task_create_user() no longer builds this itself
sched_add_task(init_task);
```

`task_create_user()`'s signature is unchanged from Phase 4 — it still just wants `user_entry`/
`user_stack_top`, both of which now come from the loader instead of hand-picked constants.
Whether it takes `ctx` as a fourth parameter and assigns it internally, or the caller assigns it
after (as sketched above), is a small call worth making deliberately rather than by habit —
either is fine as long as C0's invariant holds: `task->ctx` is never `NULL` by the time the task
is scheduled.

**Verify — the whole Step, boot to console:**

```
[TASK ] pid 3  init  READY  kstack 0x...-0x...  guard ok
[IRQ  ] Enable IRQ
hello from /bin/init
[X8664] ============= [ UNHANDLED EXCEPTION ] =============   ← init's `for(;;)` never traps;
...                                                              this line only appears if init
                                                                  faults instead of spinning
```

Two shapes are both acceptance, and worth distinguishing going in: if `init`'s backstop is an
infinite loop (like D0's sketch), the machine hangs cleanly at ring 3 after printing — read
`info registers`/`CPL` in the monitor to confirm it's spinning unprivileged, same technique
Phase 4 Step 1 used for its own backstop loop. If `init` instead falls off the end into
unmapped memory, expect a fault dump — either is fine, since **the printed string is the actual
proof**, not how the process eventually stops (Phase 7's problem, same as Phase 4 Step 1 left
it).

---

## 🕳️ Traps worth knowing about in advance

- **C0 skipped, C2 built anyway.** The loader maps into `ctx` correctly, but CR3/TTBR0 never
  points at it, so the scheduled task either runs the *previous* address space's leftover
  mappings at `0x40000000` (silent wrong behavior if something else was there) or faults on
  "not present" (if nothing was) — a mapping bug that isn't one, exactly `PHASE5_LOADING.md`
  Chapter 5's warning about copying into the wrong context, one layer up.
- **Two `PT_LOAD` segments sharing a page, second allocation wins.** C2's "reuse if already
  mapped" check exists specifically to prevent the second segment's `pmm_alloc()` from
  replacing the first segment's tail with a fresh, zeroed page.
- **`.bss` "works" without being zeroed.** A freshly `pmm_alloc()`'d page often *is* zero, so
  skipping the explicit `memset` can pass every test until a page gets reused with stale
  kernel data in it — silent, and far from the loader by the time it's noticed.
- **Forgetting decision 3 (per-arch init path).** Hard-coding `/bin/init` without the
  `#ifdef __x86_64__` split means whichever architecture built the initrd last silently
  overwrites the other's binary at the same tar path, and the *other* architecture faults
  immediately on `e_machine` — a real bug that looks like a loader bug.
- **Stack pointer 16-byte misalignment.** Same failure Phase 4 Step 1's design doc already
  flags for the general case — invisible here since `init` never calls anything, but wrong
  code shipped now is a debugging session in Phase 9, far from its cause.

---

## 📁 Files touched

New:

- ⬜ `kernel/src/proc/elf.c`, `kernel/include/proc/elf.h` — `elf_load()`, `elf_build_user_stack()`
- ✅ `user/src/init/init.c` — freestanding, inline-asm `write`, no libc (D0)
- ✅ `kernel/`, `user/`, `initrd/` build system — restructured into three ARCH-parametrized
  subprojects during D0, superseding the "two Makefile rules" plan this section originally
  described; see D0 below for the shape it took

Existing, to be modified:

- ⬜ `kernel/src/proc/task.c` — `task_create()` assigns `task->ctx = vmm_kernel_context`;
  `task_create_user()` assigns a fresh one (C0)
- ⬜ `kernel/src/proc/sched.c` — `sched_on_trap_exit()` calls `vmm_switch_context()` (C0)
- ⬜ `kernel/src/core/main.c` — the commented-out blob block replaced by `elf_load()` +
  `elf_build_user_stack()` + `task_create_user()` (C4)

---

## 🪜 Verification

- **C0** is fully testable at rest with the existing kernel threads — no ELF, no user mode,
  same tools Phase 3/4 already used (`task_dump_all()`, switch counters).
- **C1** is a pure function of bytes, same as `PHASE5_LOADING.md`'s Verification chapter says —
  worth a `src/core/test/test_elf.c` alongside the boot-log check: accept a good header, reject bad
  magic, wrong class, `ET_DYN`, and the other architecture's `e_machine`.
- **C3**'s layout and alignment given a known `argc`/`argv`/`envp` is likewise pure-data
  testable, independent of C2 or scheduling.
- **C2 and C4** are only observable by booting, per `PHASE5_LOADING.md` — the loader actually
  producing a running, printing process. Expected output is in C4 above.

One thing worth carrying forward, not fixed here: **`init` still can't stop cleanly.** Same
open item Phase 4 Step 1 left — process lifetime is Phase 7. Phase 5 Step 2 gives `init` more to
*do* (`open`/`read`/`close` against `/root/hello.txt`); it still doesn't give it a way to *exit*.
