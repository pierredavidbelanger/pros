# Working Document: Phase 5 Step 2 — The Real Syscall Surface [STATUS: COMPLETE ✅]

> [!NOTE]
> **Phase 5 Step 2**, from [`PHASE5_LOADING.md`](PHASE5_LOADING.md) Chapter 2. Read that chapter
> for the *why* — the three-layer fd design, the userland-pointer threat model, the `-errno`
> convention. This document is the *how*, in order, against the code as it stands after Step 1
> ([`PHASE5_STEP1_ELF_LOADER.md`](PHASE5_STEP1_ELF_LOADER.md) — `/bin/init` loads, runs at ring 3
> / EL0, and can call `write`, nothing else).

> [!NOTE]
> Mentor-mode reminder (see the note at the top of [`../README.md`](../../README.md)): this is a
> design reference to code from by hand, not code to paste in. Signatures are given where the
> shape is settled; several decisions below are genuinely open and want your judgment call before
> anything is written.

---

## ✅ What actually got built

All six Parts (C0-C5) are done, verified on both architectures. `/bin/init` opens
`/root/hello.txt`, reads it, writes the real contents to stdout, and calls `exit` — the whole
stack, ring 3 → syscall → per-process fd → VFS → ramfs and back, with the task staying genuinely
dead afterward rather than getting resumed. **Phase 5's stated goal is met.**

Several things diverged from the plan above, worth keeping:

- **`open` needed a real split, not just a signature.** AArch64 Linux has no bare `open` syscall
  at all — only `openat`, added after AArch64 joined the kernel. `sys_openat(dirfd, path, flags,
  mode)` is the one real implementation; `sys_open(path, flags)` is a thin wrapper calling it with
  a hardcoded `AT_FDCWD`, rejecting any other `dirfd` with `-ENOSYS` since PrOS has no per-task cwd
  to resolve one against yet. `unistd.h` deliberately breaks from "always use real Linux numbers"
  in one place to keep this clean: AArch64 gets a genuine, real `SYS_openat = 56` *and* an invented,
  PrOS-only `SYS_open = 511` (chosen clear of the real, densely-packed AArch64 syscall range) so
  `init.c`'s 2-argument `open()` wrapper stays identical in shape on both architectures instead of
  being forced into `openat`'s 4-argument form on AArch64 alone.
- **`copy_from_user`/`copy_to_user` route through a bounded kernel-side chunk buffer
  (`COPY_BUFFER_SIZE` 512 bytes), not a `kmalloc(count)` sized to the whole request** — avoiding an
  unbounded kernel allocation driven directly by a user-supplied length. `sys_read`/`sys_write`
  loop over it, and a real design question came out of that loop: what happens when a later chunk
  fails after earlier chunks already succeeded? Resolved the same way for both directions — partial
  progress is preserved and returned; the error only propagates directly when nothing has
  succeeded yet (`total_count == 0`).
- **A second bug in `sched_on_trap_exit()`, found only by tracing the full switch path.**
  `sched_pick_next()` correctly skips `TASK_DEAD` tasks, and `sched_exit_current()` correctly sets
  `need_resched` alongside the state change — but the pre-existing switch-out line,
  `current->state = TASK_READY`, predates `TASK_DEAD` and ran unconditionally, silently
  resurrecting a just-exited task back to `READY` the moment the scheduler switched away from it.
  It would then get rescheduled and resume mid-syscall as if `exit()` had simply returned — the
  exact "dead task resumed" trap Chapter 4 warned about, just arriving through a different line
  than expected. Fixed with `if (current->state != TASK_DEAD) current->state = TASK_READY;`.
- **A shared `user/include/syscall.h` header**, not originally scoped here, added once a second
  reason to duplicate syscall wrappers per program arrived. Every `static inline` wrapper
  (`sys_read`/`sys_write`/`sys_open`/`sys_openat`/`sys_close`/`sys_getpid`/`sys_exit`) lives there
  once, `#ifdef __x86_64__`/`#else` *inside* each function body rather than duplicating the whole
  signature per architecture. `user/Makefile` grew a plain `-Iinclude`. Still zero `#include`s of
  `kernel/include/` — the ABI numbers are still hand-copied on purpose, just de-duplicated across
  `user/`'s own programs instead of duplicated across them.

**One real bug, the standout one of this Step — x86_64 only, found by reading the fault, not
guessing:** `init` calling `exit()` faulted the instant the scheduler switched away to `BOOT`,
with `RIP == RCX == CR2` and an error code decoding to *present, user-mode, instruction-fetch* —
the CPU, still in ring 3, tried to execute a real kernel address. Cause: `syscall_entry.S`'s
return path always executed `sysretq`, which is a fast, return-*to-userspace*-only instruction —
it unconditionally drops to `CPL 3` no matter what `RCX` holds, with zero validation. Every syscall
before `exit` only ever resumed *the same task, back into its own userspace* (`write`/`read`/
`open`/`close`/`getpid` never force `need_resched` themselves — only the timer tick does, and
timer-triggered switches always went through `isr.S`'s `iretq`, which is a genuinely general
return that reads the target `cs` off the stack and stays in ring 0 or drops to ring 3
accordingly). `exit` is the first syscall that forces an *immediate*, syscall-triggered switch —
and the only other task around to switch to is `BOOT`, a kernel thread that should resume in ring
0. `RCX` held `BOOT`'s own valid, saved `rip` — `sysretq` just wasn't the right instruction to
resume it with. AArch64 never hit this: ARM64 has one unified `eret` that always reads the target
privilege level out of `SPSR_EL1`, no fast-path/general-path split at the ISA level the way x86_64
splits `sysretq` from `iretq`. Fixed by dropping `sysretq` entirely in favor of `iretq` — reading
`cs`/`rflags`/`rsp`/`ss` straight off the stack instead of manually loading `rcx`/`r11` — with the
`swapgs` before it made conditional on the target `cs` actually being the user selector, since a
kernel thread needs to keep running with the kernel's `GS` base active. Real Linux does the same
kind of fast-path/slow-path split in `entry_64.S`; the reason PrOS needs it in a place Linux never
would is a deliberate simplification worth keeping, not a mistake — every task here, kernel thread
or user process, gets resumed through the *same* trap-frame-return code path, where Linux keeps a
separate `switch_to()` for kernel threads that never touches `sysret`/`iret` at all.

Also worth remembering: **`core/test/test_vfs.c`'s self-test buffers broke the moment
`copy_to_user`/`copy_from_user` started actually validating.** They were kernel stack arrays —
legitimate kernel addresses, correctly rejected by the new bounds check, since the syscalls can no
longer tell "trusted kernel test code" from "hostile ring-3 process" apart. Fixed by demand-mapping
one real page below `VMM_ADDR_SPLIT` (`vmm_map_page(vmm_kernel_context, ...)`, same pattern
`test_vmm.c` already used) and threading that pointer through every sub-test instead of a local
array — which also surfaced a `sizeof(buf)` footgun (a pointer parameter's `sizeof` is `8`, not the
array size it used to be) and a string-literal-as-syscall-buffer bug (`.rodata` is a kernel address
too), both silent-wrong-behavior bugs rather than compile errors.

---

## 🎯 What "done" looks like

> `/bin/init` opens `/root/hello.txt`, reads its contents, writes them to stdout, and exits — the
> whole stack, ring 3 → syscall → per-process fd → VFS → ramfs and back. This is the sentence
> [`PHASE5_LOADING.md`](PHASE5_LOADING.md) opens with, and **Phase 5's stated goal is met here.**

**What is explicitly *not* in this step:**

- ❌ **No `fork`/`exec`.** One process, same as Step 1. `wait4`, zombie reaping, and a second
  process ever existing at once are Phase 7.
- ❌ **No `getdents64`.** `sys_readdir` stays the internal, non-ABI helper it already is;
  `PHASE5_LOADING.md` Chapter 2 flags the ABI-facing rename as Phase 6's problem, not this one.
- ❌ **No `O_CREAT`/`O_TRUNC`.** Phase 2 deliberately deferred file creation/truncation, and
  `/root/hello.txt` already exists in the initrd — this step's acceptance test never needs to
  create a file.
- ❌ **No verified-mapped `copy_from_user`.** Bounds-check only, per Chapter 2's recommendation —
  a genuine unmapped-page fault is allowed to be a fault, same as Linux's own fixup-table approach
  without needing to build one yet.
- ❌ **No real process teardown.** `exit` has to make a task *stop running*, not clean up after it
  the way a reaper eventually will — see decision 4, which is the one genuinely open question in
  this whole step.

---

## 🧠 Where this actually stands

Confirmed by reading the tree, not assumed:

- `kernel/src/fs/vfs/file.c` holds `static struct file open_files[MAX_OPEN_FILES]` — one
  **global** table, `MAX_OPEN_FILES` = 256. `sys_open`/`sys_close`/`sys_read`/`sys_write`/
  `sys_lseek`/`sys_readdir` all exist and work against it, but every failure path returns a bare
  `-1` — no `errno.h` exists yet, `SYSCALL_DESIGN.md` §4 already commits to `-errno` but nothing
  has converted.
- `kernel/src/syscall/syscall.c`'s `syscall_table` wires exactly one entry, `SYS_write`, through
  `sys_write_console_or_vfs()` — a shim that special-cases fd `1`/`2` straight to
  `console_putc()` before ever touching the fd table, and falls through to `sys_write()` (the VFS
  path) for anything else. Neither path validates the `buf` pointer it's handed.
- `kernel/include/asm/unistd.h` defines only `SYS_write` per architecture (`1` on x86_64, `64` on
  AArch64) plus `NR_SYSCALLS 512`.
- `user/src/init/init.c` hand-encodes that same syscall number and calling convention itself,
  inline, with no shared header — there's exactly one program today, so nothing forced a decision
  about sharing this yet.
- `kernel/src/proc/sched.c`'s `sched_pick_next()` just returns `current->next` around a circular
  **singly**-linked run queue — no predecessor pointer, no removal path. `task_destroy()`
  (`kernel/src/proc/task.c`) is real and correct (frees `ctx` only if owned, frees the kernel
  stack) but is only ever called from `core/test/test_task.c` today — never from live scheduling
  code. Nothing currently makes a task stop being scheduled.

---

## ⚖️ Decisions to make before writing anything

### 1. The fd table's shape: flat array, or a level of indirection?

Chapter 2 sketches `task->fds[fd] → struct file (shared, refcounted) → struct vfs_node`, so that
`fork` can later duplicate the *array* while `dup`/`fork`'d fds keep sharing one open-file
description (and its offset — that's specified POSIX behaviour, not an accident). `struct file`
already carries `ref_count` (`include/fs/vfs/file.h`), built for `close` but exactly the field
this needs.

**Recommendation: build the indirection now** — `struct file *fds[MAX_OPEN_FILES]` per task, each
slot either `NULL` or a heap-allocated, refcounted `struct file` — even though nothing shares an
fd table yet and `ref_count` will only ever go `1 → 0`. Retrofitting a pointer layer once `fork`
exists is a bigger, more error-prone change than starting one level indirect. The alternative
(flat `struct file fds[MAX_OPEN_FILES]` per task, no heap allocation) is simpler and *works* for
this step alone — worth naming as the deliberately-smaller option if the indirection feels like
scope creep for a step that's supposed to be about reachability, not about `fork`.

### 2. fd `0`/`1`/`2` collide with the console shim

`sys_write_console_or_vfs()` already intercepts fd `1`/`2` before the fd table is ever consulted.
`allocate_fd()` scans from index `0` — so the very first real `sys_open()` today would hand back
fd `0`, colliding with stdin's reserved number the instant anything assumes `0`/`1`/`2` mean
console. Real Unix pre-opens `0`/`1`/`2` onto an actual console device at process start; PrOS's
shortcut has been to special-case `1`/`2` in the dispatcher and skip a console `vfs_node`
entirely.

Two ways out:

- **(a) Reserve slots `0`-`2`** — `allocate_fd()` starts scanning at `3`, the dispatcher shim
  stays exactly as it is. Smaller change, keeps this step scoped to file I/O.
- **(b) Build a minimal `/dev/console` `vfs_node`** now, pre-populate fd `0`/`1`/`2` against it,
  retire the shim entirely.

**Recommendation: (a).** (b) is more correct but drags a device driver into a step that's about
the syscall surface — worth doing when Phase 6 needs a real `/dev/console` for the TTY line
discipline anyway, where it stops being extra scope and starts being required.

### 3. `copy_from_user` / `copy_to_user`: bounds-check only

Matches Chapter 2 directly, restated here because C2/C3 below depend on the exact rule: the range
`[user_ptr, user_ptr + len)` must lie entirely below `VMM_ADDR_SPLIT`
(`kernel/include/mm/vmm.h`), and `user_ptr + len` must not **wrap** — check for overflow before
comparing, not after, or the wraparound case silently passes. No mapped-page verification; an
unmapped access takes a genuine fault and that's allowed to happen. Worth applying to `sys_write`
too, not just the new `sys_read` — right now `sys_write_console_or_vfs()` writes straight from a
user-supplied `buf` with zero validation, on the VFS path *and* the console path.

### 4. `sys_exit`'s minimal semantics — the one genuinely open design question

The problem found while reading `sched.c` above: nothing today can make a task stop being
scheduled. `sys_exit()` needs the scheduler to never run this task again, without: removing a
node from a **singly**-linked circular list mid-traversal (no predecessor pointer exists to patch
around it), freeing a kernel stack while a syscall is still executing on top of it
(`task_destroy()` on yourself, from inside yourself, frees the very stack your own return address
lives on — an instant use-after-free), or any of `fork`/`wait`/zombie-reaping, all Phase 7.

**Recommendation:** add a `TASK_DEAD` state next to `TASK_READY`/`TASK_RUNNING`
(`kernel/include/proc/task.h`). `sys_exit()` marks `current` dead; `sched_pick_next()` skips any
`TASK_DEAD` task when walking the ring, but never unlinks or frees anything. The task's memory —
context, kernel stack, the `struct task` itself — sits inert until Phase 7 has a real reaper. A
named, accepted leak, same spirit as `/bin/init` itself never being able to stop cleanly, which
Step 1 already carried forward deliberately rather than silently.

**The subtler half of this decision, found by tracing the actual return path:** both
`x86_64_syscall_handler()` and `aarch64_exception_handler()`'s SVC branch call
`sched_on_trap_exit(frame)` unconditionally after every syscall — but `sched_on_trap_exit()` only
*switches* when `need_resched` is true, and today only the timer tick ever sets that. If
`sys_exit()` only flips `current->state` and returns, `need_resched` is still `false`: the
dispatcher hands the same frame straight back to `iretq`/`eret`, and **the just-exited task's own
code resumes running** until the next timer interrupt happens to fire. Marking a task dead has to
force an immediate switch, not wait for one. Concretely, this wants a scheduler entry point
`sys_exit()` can call that does both halves atomically:

```c
// include/proc/sched.h
// Marks the current task TASK_DEAD and forces the next sched_on_trap_exit() to switch away from
// it — the syscall return path must never resume a dead frame.
void sched_exit_current(void);
```

Worth deciding explicitly, even though it can't happen yet (`BOOT` never exits): what does
`sched_pick_next()` do if it ever walks the *entire* ring and finds every task dead? Recommend an
explicit `kpanic("no runnable tasks")` rather than an undefined spin — cheap to add now, and it
turns a future silent hang into a diagnosable panic, the same trade the trap-frame ownership check
already makes elsewhere in `sched_on_trap_exit()`.

### 5. `getpid`

Trivial — `sched_get_current_task()->pid`. Only real decision is where `sys_getpid()` lives:
recommend `task.c`, next to the other task-shaped operations, keeping `syscall.c` pure
dispatch/glue rather than reaching into `proc/sched.h` directly.

---

## 🗺️ Suggested order

All of this step is architecture-neutral — the only per-architecture surface is a few new
constants in `unistd.h`, not a new code path, so there's no A/B split to declare.

```
  C0 ── errno.h + convert file.c's syscalls to -errno
  C1 ── per-task fd table (decision 1), task_inner_create()/task_destroy() grow it
  C2 ── copy_from_user / copy_to_user (decision 3)
  C3 ── extend unistd.h + syscall_table: open, read, close, getpid
  C4 ── sys_exit + TASK_DEAD + sched_exit_current() (decision 4) — independent, do anytime after C0
  C5 ── wire it: init.c opens /root/hello.txt, reads, writes, exits
```

C0 first, since `-errno` is the convention every later Part's new code should already be written
in, not retrofitted into. C1 blocks C3's `open` (needs somewhere to put the fd). C2 blocks C3's
`read` (needs to validate its `buf`). C4 has no real dependency on C1-C3 and is worth doing
whenever it's convenient — but C5, the acceptance test, needs `exit` reachable at all, or `init`
runs off the end the way Phase 4's blob and Step 1's `init` both still do.

---

## 🧩 C0 — `errno.h` and the `-errno` conversion ✅ DONE (2026-08-17)

`kernel/include/errno.h`, the values this step's own syscalls can actually produce (Linux's real
numbers, per `SYSCALL_DESIGN.md` §4):

```c
#define ENOENT  2
#define EBADF   9
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define EINVAL 22
#define ENOSYS 38
```

Convert every `-1` in `file.c` to the specific `-Exxx` for that failure — bad fd → `-EBADF`,
lookup failure → `-ENOENT`, unsupported operation → `-EINVAL` or `-ENOSYS` depending on which
fits — one pass, not mixed, per Chapter 5's warning about a half-converted error space being
worse than either convention alone. `syscall_dispatch()`'s own catch-all for an unknown syscall
number becomes `-ENOSYS` (`syscall.c:24` already has a comment marking that exact spot).

**Verify:** a pure-data self-test (`core/test/`) calling each `sys_*` with a deliberately bad
fd/path and checking the exact `-Exxx` code — no boot required, same "pure function of bytes"
testability `PHASE5_LOADING.md`'s Verification chapter calls out for this whole step.

---

## 🧩 C1 — Per-task file descriptors ✅ DONE (2026-08-17)

- `task->fds` gains the array (decision 1's shape), still sized `MAX_OPEN_FILES` — worth
  reconsidering whether 256 *per task* is still the right number now that it's not global.
- `allocate_fd()` moves from a free function in `file.c` into a per-task scan, starting at index
  `3` if decision 2 goes with (a).
- `task_inner_create()` zero-initializes the array; `task_destroy()` drops a reference on every
  non-`NULL` slot — this is also where a future `sys_exit`-time close-everything would hook in,
  once Phase 7 actually frees a dead task.

**Verify:** open the same path from two different tasks' fd tables (fake tasks are enough, no
scheduling needed) and confirm the fds don't collide, and closing one doesn't affect the other.

---

## 🧩 C2 — `copy_from_user` / `copy_to_user` ✅ DONE (2026-08-17)

```c
// include/mm/uaccess.h
// Bounds-checks [user_ptr, user_ptr + len) against VMM_ADDR_SPLIT, rejecting overflow before
// comparing. Returns 0 on success, -EFAULT if the range is invalid. Does not verify mappings —
// an unmapped access is allowed to fault for real.
int copy_from_user(void *kernel_dst, const void *user_src, uint64_t len);
int copy_to_user(void *user_dst, const void *kernel_src, uint64_t len);
```

`sys_read`'s and `sys_write`'s `buf` route through these before touching `f->node->ops` — and so
does `sys_write_console_or_vfs()`'s console fast path, which bypasses `sys_write()` entirely today
and therefore bypasses whatever validation lives only inside it.

**Verify:** unit-test the bounds check in isolation — a good range, a range that starts below
`VMM_ADDR_SPLIT` but wraps past it on the addition, an address entirely above the split. Pure
arithmetic on addresses, no paging or mapping involved, so no boot required.

---

## 🧩 C3 — `open`/`read`/`close`/`getpid` reachable from ring 3 ✅ DONE (2026-08-17)

- `unistd.h` grows `SYS_open`/`SYS_read`/`SYS_close`/`SYS_getpid` per architecture — Linux's real
  numbers, same source-of-truth discipline `SYSCALL_DESIGN.md` §2 already states
  (`syscall_64.tbl` for x86_64, `asm/unistd.h` for AArch64).
- `syscall_table` picks them up — `sys_read`/`sys_close` already exist in `file.c`, this is
  wiring plus the fd-table/`copy_from_user` changes from C1/C2 flowing through them.
- `user/src/init/init.c` grows matching inline-asm wrappers, same shape as its existing
  `sys_write`. Worth asking here whether to keep hand-duplicating syscall numbers per program, or
  start a small `user/`-side-only shared header now that there's a second program's worth of
  boilerplate arriving — **not** `kernel/include/asm/unistd.h` itself, per
  [[feedback_userland_kernel_boundary]]: `user/` never `#include`s a kernel header, ABI facts get
  hardcoded on the user side deliberately.

**Verify:** boot-only, same as Step 1's C2/C4 — no shortcut for "does the trap frame really carry
the right values to ring 0 and back," per `PHASE5_LOADING.md`'s Verification chapter.

---

## 🧩 C4 — `sys_exit`, `TASK_DEAD`, and `sched_exit_current()` ✅ DONE (2026-08-17)

Per decision 4: `TASK_DEAD` added to `task.h`, `sched_pick_next()` skips it, and `sys_exit()`
calls `sched_exit_current()` rather than touching `current->state` directly — the whole point is
that marking-dead and forcing-a-switch happen together, so the syscall return path can never hand
control back to a task that's already decided to stop existing.

**Verify:** from a test harness (not full boot), fabricate three tasks, mark the current one dead
via `sched_exit_current()`, and confirm `sched_pick_next()` never returns it again across several
calls. Then, at boot: `init` calls `exit` instead of spinning forever, and the machine reaches
`arch_shutdown()` without ever printing an unhandled-exception dump for `init`.

---

## 🧩 C5 — Wire it: `init` opens, reads, writes, exits ✅ DONE (2026-08-17)

```c
// user/src/init/init.c — the actual acceptance test
int fd = sys_open("/root/hello.txt", O_RDONLY);
char buf[128];
long n = sys_read(fd, buf, sizeof(buf));
sys_write(1, buf, n);
sys_close(fd);
sys_exit(0);
```

Nothing about `main.c`'s loading sequence changes here — `elf_load()`/`elf_build_user_stack()`/
`task_create_user()` are unchanged from Step 1. This Part is purely `init.c` exercising the new
syscalls and the kernel's C0-C4 supporting them.

**Verify — the whole Step, boot to console:**

```
[TASK ] pid 3  /bin/init  READY  kstack 0x...-0x...  guard ok
[IRQ  ] Enable IRQ
hello from /root/hello.txt        ← whatever tar_load() actually populated hello.txt with
[K    ] All done here, shutting down.
```

No unhandled-exception dump for `init` — it should reach `sys_exit` and stay dead, not fault off
the end the way Step 1 deliberately left it.

---

## 🕳️ Traps worth knowing about in advance

- **A dead task resumed anyway.** ✅ Hit for real, twice, via two different mechanisms — see
  "What actually got built" above. The predicted half (`need_resched` never getting set) was
  avoided by `sched_exit_current()` setting it atomically with the state change. The
  *unpredicted* half — `sched_on_trap_exit()`'s pre-existing unconditional
  `current->state = TASK_READY` silently un-killing the task on the very next switch-out — wasn't
  in this doc at all, and needed the resurrection to actually happen at boot to be found.
  x86_64 additionally resurrected a dead task's *execution*, not just its state, via the
  `sysretq`/`iretq` bug — same trap category, a third mechanism, arrived at independently.
- **fd `0`/`1`/`2` colliding with the console shim.** ✅ Avoided — decision 2(a) shipped as
  planned, `allocate_fd()` scans from index `3`, the console shim never sees a real fd collision.
- **A half-converted `-errno` space** (Chapter 5's warning, restated because C0 is exactly where
  it would happen): callers checking `< 0` and callers checking `== -1` disagreeing about what
  `-1` even means, mid-conversion.
- **`copy_from_user`'s wraparound case.** `base + len` overflowing past `VMM_ADDR_SPLIT` has to be
  checked as an overflow, not just compared after the addition — the addition itself can wrap to
  a small number that passes a naive bounds check.
- **Freeing a stack out from under the syscall running on it.** Any temptation to have `sys_exit`
  call `task_destroy()` on `current` synchronously is the same class of bug as the dead-task-
  resumed trap above, one layer worse — this one is a genuine use-after-free of the return
  address, not just a resumed frame.

---

## 📁 Files touched

New:

- ✅ `kernel/include/errno.h`
- ✅ `kernel/include/mm/uaccess.h`, `kernel/src/mm/uaccess.c` — `copy_from_user`/`copy_to_user`
- ✅ `user/include/syscall.h` — not originally scoped, see "What actually got built" above

Existing, to be modified:

- ✅ `kernel/src/fs/vfs/file.c` + `kernel/include/fs/vfs/file.h` — `-errno` throughout, fd table
  moves off the global `open_files[]` and onto `struct task`
- ✅ `kernel/src/proc/task.c` + `kernel/include/proc/task.h` — per-task `fds[]`, `TASK_DEAD`
- ✅ `kernel/src/proc/sched.c` + `kernel/include/proc/sched.h` — `sched_pick_next()` skips dead
  tasks, new `sched_exit_current()`, the `TASK_READY` resurrection fix
- ✅ `kernel/src/syscall/syscall.c` + `kernel/include/core/syscalls.h` — `sys_exit`, `sys_getpid`,
  `syscall_table` entries for `read`/`write`/`open`/`openat`/`close`/`getpid`/`exit`
- ✅ `kernel/include/asm/unistd.h` — `SYS_read`/`SYS_write`/`SYS_open`/`SYS_openat`/`SYS_close`/
  `SYS_getpid`/`SYS_exit` per architecture, including the AArch64 `SYS_open` invention
- ✅ `kernel/src/arch/x86_64/syscall_entry.S` — not originally scoped; `sysretq` → `iretq` +
  conditional `swapgs`, see "One real bug" above
- ✅ `kernel/src/core/test/test_vfs.c` — not originally scoped as a *rework*; buffers moved off
  the kernel stack onto a real mapped page once `copy_to_user`/`copy_from_user` started validating
- ✅ `user/src/init/init.c` — the real open/read/write/close/exit body (C5), now just
  `#include "syscall.h"` plus program logic

---

## 🪜 Verification

- **C0** is a pure function of inputs → error codes, testable without booting.
- **C1**'s fd allocation/exhaustion and **C2**'s bounds check (including the wraparound case) are
  likewise pure-data testable, independent of scheduling or paging.
- **C4**'s dead-task-skipped invariant was verifiable with fabricated tasks and no boot, as
  planned — in practice it ended up verified at boot instead, which is exactly what surfaced the
  `TASK_READY` resurrection bug ("What actually got built" above). A fabricated-task unit test
  would have covered `sched_pick_next()` correctly but never exercised
  `sched_on_trap_exit()`'s switch-out path, so it wouldn't have caught this one anyway — worth
  remembering as a real example of "pure function of state" testing missing a bug that only
  exists in the transition between states.
- **C3 and C5** are only observable by booting, same as Step 1's C2/C4 — the loader and syscall
  path actually producing a process that opens a real file and prints its real contents. Expected
  output is in C5 above.

One thing worth carrying forward, not fixed here: this step still doesn't give `init` a *parent*
to report its exit status to, or a way for anything to notice it died — `sched_exit_current()`
only stops the scheduler from resuming it. `wait4` and a real exit status are Phase 7's problem,
same as `fork` itself.
