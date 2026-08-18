# Working Document: Phase 6 Step 2 — TTY Line Discipline and a Real `/dev/console` [STATUS: NOT STARTED ⬜]

> [!NOTE]
> **Phase 6 Step 2**, from [`PHASE6_TALK_BACK.md`](PHASE6_TALK_BACK.md) Chapter 2. Read that
> chapter for the *why*. This document is the *how*, against the code as it stands after Step 1
> ([`PHASE6_STEP1_UART_INPUT.md`](PHASE6_STEP1_UART_INPUT.md) — a real interrupt-driven byte
> queue exists on both architectures; nothing reads it for a human-facing reason yet).

> [!NOTE]
> Mentor-mode reminder (see the note at the top of [`../README.md`](../README.md)): this is a
> design reference to code from by hand, not code to paste in. Signatures are given where the
> shape is settled; several decisions below are genuinely open and want your judgment call before
> anything is written.

---

## 🎯 What "done" looks like

> A test program calls `sys_read(0, buf, n)`, types a line at the keyboard including a typo it
> backspaces over, and gets back exactly the corrected line — through a real `/dev/console`
> `vfs_node`, not a syscall-level special case.

All of this Step is architecture-neutral — Step 1 already absorbed every real per-architecture
difference into one shared byte queue, so nothing here needs an A/B split.

**What is explicitly *not* in this step:**

- ❌ **No raw mode.** Canonical (line-buffered, editable) is the only mode `psh` needs. Raw mode
  is a real POSIX TTY concept, deferred to Phase 10 (PTYs) on purpose.
- ❌ **No real blocking.** See decision 4 below — this step's `read()` spins, it doesn't sleep.
  A genuine wait queue is Phase 7's `switch_to` and blocking work, same as `fork`/`wait`.
- ❌ **No `/dev` directory enumeration.** `/dev/console` resolves by exact-path mount, per
  Chapter 2's note that `vfs_get_mountpoint()`'s longest-prefix matching needs no parent
  directory to exist. A real `/dev` a shell could `ls` is more scope than one device node needs.

---

## 🧠 Where this actually stands

Confirmed by reading the tree, not assumed:

- `console_putc()` (`kernel/src/core/console.c:19-26`) fans out unconditionally to
  `serial_putc()` and `fb_terminal_print_char()` — no fd awareness, no buffering, works exactly
  as well for this Step's echo needs as it always has.
- The shim being retired, exactly as it stands (`kernel/src/syscall/syscall.c:8-16`): fd `1`/`2`
  special-cased straight to `console_putc()`, bypassing `copy_from_user` entirely, with a comment
  already naming this Step as the fix.
- `struct vfs_ops` (`kernel/include/fs/vfs/vfs.h:18-33`) already supports everything a device
  node needs — `open`/`close`/`read`/`write` function pointers, `priv_data` for driver state, no
  `ramfs` coupling. `VFS_CHARDEVICE` (`vfs.h:12`) already exists as a flag, unused anywhere yet.
- `vfs_mount()` has only ever been exercised on `ramfs` mounting `/`. This Step is the first time
  anything mounts a node that isn't backed by `ramfs`'s storage at all — worth treating that as a
  real, if small, unknown until it's actually tried, not an assumed-safe reuse.
- fd allocation already starts at index `3` (`file.c`, since Phase 5 Step 2's decision 2(a)) —
  fds `0`-`2` are reserved but **nothing pre-opens them onto anything today.** A task's `fds[]`
  array starts fully `NULL`.

---

## ⚖️ Decisions to make before writing anything

### 1. `/dev/console`'s `struct file` state

A device node still goes through the same `struct file`/fd-table machinery as any other open
file (`kernel/include/fs/vfs/file.h:8-13`) — but `offset` means nothing for a character stream.
**Recommendation:** leave `offset` untouched (never incremented), and make `sys_lseek` on a
`VFS_CHARDEVICE` node return `-ESPIPE` (the real Linux errno for exactly this — "illegal seek on
a pipe/device") rather than a number that would silently mean nothing.

### 2. fd `0`/`1`/`2` at task creation

Chapter 2 leans toward pre-opening them for real. **Recommendation:** `task_inner_create()`
(`kernel/src/proc/task.c`) opens `/dev/console` three times (or once, with `ref_count` bumped
twice more — see Phase 5 Step 2's `struct file` refcounting, already built for `close`) and
installs the result at `fds[0]`, `fds[1]`, `fds[2]`, for every task, kernel threads included.
This is what actually retiring the shim means — `sys_write_console_or_vfs()`'s fd `1`/`2` check
goes away entirely once `sys_write(1, ...)` reaches a real, valid fd through the normal path.

### 3. The line buffer's home

**Recommendation:** per-open, via the `struct file`'s `priv_data` (already exists in
`vfs_ops`-adjacent design, confirm the exact field name against `vfs.h`) — not one global buffer.
A global buffer works with exactly one reader, which happens to be true the moment this lands,
but a second task ever opening `/dev/console` (a second shell, someday) would silently corrupt
the first one's in-progress line. Per-open costs one small allocation per `open()` and removes
the assumption entirely.

### 4. What `read()` does with no line ready yet — the one genuinely open question

PrOS has no blocking primitive at all — Phase 7's own scope list names `switch_to` and blocking
(wait queues, voluntary switching) as work the frame-swap scheduler "deliberately doesn't cover"
yet. So `sys_read(0, buf, n)` calling into `/dev/console`'s `read` op with no completed line in
the queue has exactly one honest option today: **spin inside the syscall handler**, checking for
a completed line, until one exists — not sleep, because nothing exists to sleep on.

This is sound, not just tolerable, given how this project's scheduler already works: interrupts
stay enabled during the spin, so Step 1's UART ISR keeps firing and filling the queue while the
syscall handler loops. And because PrOS is *preempted at trap exit* rather than cooperatively
preemptible (`README.md`'s own framing), a timer tick firing *while* a task is mid-spin, deep
inside this syscall's C call stack, switches away from it exactly the same way it already switches
away from `BOOT`'s own main loop — the hardware interrupt mechanism captures the full machine
state at whatever instruction was executing, spin loop included, and resumes it later
transparently. Nothing about this needs new scheduler support; it's a direct, already-proven
consequence of how `sched_on_trap_exit()` already works. The spinning task just doesn't make
forward progress until a line completes — it doesn't starve anything else, the same way `BOOT`'s
infinite loop doesn't starve `init`.

**A real wait queue — sleeping instead of spinning, waking exactly when a line completes instead
of polling for it — is Phase 7's problem, same as `fork`/`wait`.** Naming this now rather than
discovering it as a surprise mid-Step matters: a spinning single-core kernel with only one or two
tasks alive is invisible; it stops being invisible the moment a second task actually wants CPU
time while the first blocks on input, which is precisely `psh`'s and `fork`'s situation once
Phase 7 exists.

---

## 🗺️ Suggested order

```
  C0 ── the /dev/console vfs_node + vfs_ops: open/close trivial, write wraps console_putc()
  C1 ── the line discipline: read() spins on the byte queue, echoes, handles backspace
  C2 ── mount /dev/console, pre-open fds 0/1/2 at task creation (decision 2)
  C3 ── retire sys_write_console_or_vfs(); sys_write(1, ...) is now just sys_write()
  C4 ── wire it: a test program reads an edited line for real
```

`C0` before `C1` — the line discipline is what `read` actually does, so the node needs to exist
first. `C2` depends on `C0` (something to open). `C3` depends on `C2` (fd `1`/`2` must resolve
for real before the special case can safely disappear). `C4` is the acceptance test.

---

## 🧩 C0 — The `/dev/console` node

**Goal:** a `struct vfs_node` with `VFS_CHARDEVICE` set, backed by `vfs_ops` whose `write` calls
`console_putc()` per byte and whose `open`/`close` are trivial (nothing to allocate at the node
level — see decision 3 for where the per-open state actually lives).

```c
// fs/console/console_dev.c (new file — or wherever device nodes end up living; this is the
// first one, so its home is itself a small decision)
struct vfs_node *console_dev_create(void);
```

**Verify:** `vfs_mount("/dev/console", console_dev_create())` followed by
`vfs_lookup("/dev/console")` returns the same node — proves `vfs_mount`/`vfs_lookup` genuinely
tolerate a non-`ramfs` node, not assumed from reading the code alone.

---

## 🧩 C1 — The line discipline

**Goal:** `read()` on an open `/dev/console` file drains Step 1's byte queue one byte at a time,
echoing each one back through `console_putc()`, until a complete line exists, then returns it.

- **Backspace** (`0x08` or `0x7f` — check what actually arrives from a real terminal before
  assuming one) removes the last buffered byte and emits a destructive-backspace echo (`\b \b`)
  so the character visually disappears, not just logically.
- **Line completion** (`\r` or `\n`) hands the buffered line to the caller and resets the buffer
  for the next line.
- **The spin** — decision 4. `input_queue_pop()` (Step 1) returning nothing means "keep looping,"
  not "return early with whatever's been typed so far."

**Verify:** pure-data for the editing logic itself — feed a byte sequence including a backspace
into the state machine, confirm the resulting line matches what a human would expect to see, no
queue or hardware involved for this part.

---

## 🧩 C2 — Pre-open fds `0`/`1`/`2`

**Goal:** every task, from `task_inner_create()` onward, starts with `fds[0]`, `fds[1]`,
`fds[2]` already pointing at a real, open `/dev/console`. Decision 2's `ref_count` bump (or
triple-open) means `sys_close(1)` on one of them doesn't tear down the underlying node while the
other two references are still live — same refcounting discipline Phase 5 Step 2 already built.

**Verify:** a fresh task's `fds[0..2]` are non-`NULL` and resolve to the console node, without
having called `sys_open` itself.

---

## 🧩 C3 — Retire the shim

**Goal:** `sys_write_console_or_vfs()` (`syscall.c:8-16`) goes away. `syscall_table[SYS_write]`
points straight at `sys_write()` — the same function every other fd already uses, now genuinely
correct for fd `1`/`2` too because C2 made those real, valid, `copy_from_user`-validated file
descriptors instead of a special case that bypassed validation entirely.

**Verify:** grep confirms no remaining reference to `sys_write_console_or_vfs` anywhere in the
tree once this lands.

---

## 🧩 C4 — Wire it: a real edited line, read back

**Goal:** the actual acceptance test. A test program (or a throwaway addition to `init`, or a
dedicated new one — worth deciding which, same kind of call Phase 5 Step 2 made for its own C5)
calls `sys_read(0, buf, sizeof(buf))`, a human types `helllo<backspace>o\n` over `-serial stdio`,
and the program receives exactly `hello`.

**Verify — boot to console, both architectures:**

```
[TASK ] pid N  /bin/whatever  READY  ...
type here: hell<backspace>llo
you typed: hello
```

No unhandled-exception dump. The echoed line on screen shows the backspace actually erasing the
typo, not just the raw bytes sent.

---

## 🕳️ Traps worth knowing about in advance

- **A half-retired shim.** C2 and C3 have to land together in spirit — fd `1`/`2` meaning "the
  old special case" for some callers and "a real fd" for others at the same time is the same
  half-converted-space trap `-errno`'s Phase 5 conversion already warned about, one layer up.
- **The spin loop silently starving nothing, until it silently starves something.** Decision 4's
  reasoning holds for today's task count. It's worth a one-line comment at the spin site saying
  *why* it's a spin and not a sleep, so Phase 7 doesn't have to rediscover this reasoning from
  scratch when a wait queue finally replaces it.
- **`vfs_mount()`'s first non-`ramfs` exercise.** Anything in the mount-table or lookup path that
  quietly assumed "every mounted node is `ramfs`-backed" — even something as small as a debug
  dump that reaches into `ramfs`-specific `priv_data` — is worth grepping for before trusting C0
  works just because it compiles.
- **Backspace's actual byte value.** `0x7f` (DEL) and `0x08` (BS) are both real, and which one a
  given terminal sends is a real-world detail worth confirming against what QEMU's `-serial
  stdio` actually forwards, not assumed from a spec.

---

## 📁 Files touched

New:

- ⬜ the `/dev/console` `vfs_node`/`vfs_ops` implementation (C0) — exact file location TBD
- ⬜ the line-discipline state machine (C1) — architecture-neutral, no hardware dependency

Existing, to be modified:

- ⬜ `kernel/src/proc/task.c` — pre-open `fds[0..2]` in `task_inner_create()` (C2)
- ⬜ `kernel/src/syscall/syscall.c` — `sys_write_console_or_vfs()` removed, `syscall_table`
  entry repointed (C3)
- ⬜ `kernel/src/fs/vfs/file.h` / `file.c` — `-ESPIPE` on `sys_lseek` for a `VFS_CHARDEVICE` node
  (decision 1)
- ⬜ `kernel/src/core/main.c` or `user/src/init/init.c` — wherever C4's acceptance test actually
  runs

---

## 🪜 Verification

- **C0**'s `vfs_mount`/`vfs_lookup` round trip and **C1**'s line-editing state machine are both
  testable without booting — the first is a VFS-layer check with a fabricated node, the second is
  pure data (bytes in, edited line out).
- **C2, C3, C4** are only observable by booting — real fds pre-opened on a real task, a real
  syscall table entry gone, a real human-typed line arriving correctly.

One thing worth carrying forward, not fixed here: **the spin-instead-of-sleep in C1 is a named,
deliberate stopgap, not an oversight** — same spirit as Phase 5's `TASK_DEAD` leak or `init` never
having a parent to report to. Phase 7's wait queues are what replace it for real.
