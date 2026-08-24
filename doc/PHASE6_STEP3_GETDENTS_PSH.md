# Working Document: Phase 6 Step 3 — `getdents64` and `psh` [STATUS: NOT STARTED ⬜]

> [!NOTE]
> **Phase 6 Step 3**, the last one, from [`PHASE6_TALK_BACK.md`](PHASE6_TALK_BACK.md) Chapter 3 —
> which deliberately left it undesigned until Steps 1-2 were built rather than planned. They are
> now built, so this is written against the real tree, and it inherits three things the original
> planning did not assume: a real `/dev/console` on fds `0`/`1`/`2`, a preemptible syscall path,
> and `struct spinlock`.

> [!NOTE]
> Mentor-mode reminder (see the note at the top of [`../README.md`](../README.md)): this is a
> design reference to code from by hand, not code to paste in. **All six decisions below are
> resolved** (2026-08-23). Signatures are given where the shape is settled.

---

## 🎯 What "done" looks like

> PrOS boots to a `psh$` prompt. You type `ls /root`, and it prints `hello.txt`. You type
> `cat /root/hello.txt`, and it prints the file. You type `exit`, and the machine shuts down
> because the last program ended — not because a timer ran out.

This is the Phase's payoff, and the first time PrOS is *interactive* rather than merely
responsive. Everything in this Step is architecture-neutral — the `C` track only, no A/B split.
Steps 1 and 2 absorbed every real per-architecture difference already.

**What is explicitly *not* in this step:**

- ❌ **No `fork`/`exec`.** Builtins only. `psh` runs everything inside its own process because
  there is no way to start a second one yet. That's Phase 7, and it's why the builtin list is
  short and boring on purpose.
- ❌ **No pipes, redirection, globbing, `PATH`, or quoting.** Split the line on spaces and stop.
  Each of those is a real feature that wants a real parser, and none of them demonstrate anything
  this Step is trying to prove.
- ❌ **No arrow keys, history, or tab completion.** All three need raw mode so the shell can see
  bytes as they arrive; canonical mode is the only mode Step 2 built, deliberately. Phase 10.
- ❌ **No working directory.** `sys_openat` accepts `AT_FDCWD` and returns `-ENOSYS` for any other
  `dirfd` (`file.c:65`); there is no per-task cwd anywhere in the tree. **Every path `psh` takes
  is absolute**, and that is a design constraint, not an oversight to work around.

---

## 🧠 Where this actually stands

Confirmed by reading the tree, not assumed:

- **`sys_readdir()` already exists** (`kernel/src/fs/vfs/file.c:113`) and works: it uses
  `f->offset` as the directory index, calls `ops->readdir(node, index, out)`, increments the
  offset on a hit, and returns `1` / `0` / `-errno`. `ramfs_ops_readdir` implements it and the VFS
  self-tests exercise it.
- **It is not in `syscall_table`** (`syscall.c`) — no `SYS_readdir` entry, no number in
  `asm/unistd.h`. It is a kernel-internal call today, reachable only from tests.
- **⚠️ It writes `out` straight through to the driver, with no `copy_to_user`.** `ramfs_ops_readdir`
  does a `snprintf` into `out->name`. Today that's harmless because the only callers are in the
  kernel — but the moment a syscall number points at it, the kernel is `snprintf`-ing into an
  unvalidated ring-3 pointer. **This is a real latent hole and this Step is what would have made
  it exploitable** — closed by decision 1 deleting the function outright, rather than by patching
  it. See decision 2.
- **`struct vfs_dirent`** (`vfs.h:45`) is `char name[128]` + `uint32_t flags`. Fixed-size, no
  inode, no Linux `d_type`. That's the *internal* shape and it's fine; the Linux shape is packed
  and variable-length, so packing is a syscall-layer job, not a filesystem-layer one.
- **`vfs_node.inode` is never assigned anywhere in `fs/`** — it is always `0`. Worth knowing before
  deciding what `d_ino` should carry.
- **`user/` has no string functions at all.** `user/include/` holds exactly one header,
  `syscall.h`. `psh` cannot compare a command name without something new, and per the
  userland/kernel boundary rule it cannot reach into `kernel/include/stdc.h` to get one.
- **`main()` hardcodes `elf_load("/bin/init", …)`** (`main.c:117`), and the boot task then loops
  for a fixed number of seconds and calls `arch_shutdown()` (`main.c:140-153`). A fixed timeout is
  actively wrong for an interactive shell — see decision 5.
- **The kernel command line is already parsed**, crudely: `strstr(cmdline, "pros.tests")`
  (`main.c:75`). There is a precedent for a `pros.*` knob, and no parser to speak of.
- **`LDISC_BUF_SIZE` is 256** (`ldisc.h:6`), and a completed line arrives from `read()` **with its
  trailing `\n` included** (`ldisc.c`, the `'\r'`/`'\n'` case appends one).

---

## ⚖️ Decisions — resolved 2026-08-23

All six were taken before any code was written. Each keeps the reasoning that produced it, with
the outcome stated first — the alternatives are left in place because *why* something was chosen
is worth more than the choice.

### 1. `sys_readdir` — replace it, or keep it underneath? ✅ RESOLVED

> **`sys_readdir()` is removed. `ops->readdir(node, index, struct vfs_dirent *)` stays exactly as
> it is, as the implementation `sys_getdents64()` is built on.**

One syscall-layer entry point per concept, rather than two shapes for one idea — the older,
narrower one has no callers left in userland's future and would only ever be the thing someone
reaches for by mistake.

**The one real consequence:** `sys_readdir` has exactly one caller today,
[`test_vfs.c:63`](../kernel/src/core/test/test_vfs.c) —
`while (sys_readdir(fd, &entry) == 1)`, which looks for `hello.txt` by name rather than by count.
That loop has to be rewritten against `sys_getdents64()`. That is a gain, not a tax: the rewritten
version has to walk packed entries by `d_reclen`, which is exactly the check C0 needs and the one
that catches the alignment bug. **Rewrite it to keep the find-by-name property** — the existing
comment explains why (adding entries to `initrd.tar` must not break the test), and that reasoning
survives the change.

*Kept for the trail:* `getdents64` is a different shape from `sys_readdir` — one call returns
*many* packed entries, sized to a caller-supplied buffer, rather than one fixed-size entry per
call. **The rejected alternative** was keeping `sys_readdir()` alongside as a kernel-internal
helper so the tests wouldn't need touching. Cheaper in the moment, and it would have left a second
directory-reading entry point whose only justification was one test loop.

**What did not change:** `sys_getdents64()` is still built *on top* of the internal call — loop
`ops->readdir`, packing entries until the next one wouldn't fit. **Nothing about the filesystem
layer needs to learn what Linux's ABI looks like**; that knowledge belongs in one function at the
syscall boundary, the same way `-errno` and `copy_from_user` already do.

### 2. The `copy_to_user` hole ✅ RESOLVED — by decision 1, with nothing left to patch

> **Nothing to fix. Decision 1 deletes the function that had the hole.**

The unvalidated path was `sys_readdir` handing its caller's `out` pointer to a driver that
`snprintf`s into it. Removing `sys_readdir` removes the path, and no comment is needed on a
function that no longer exists — which is a better outcome than the marked-but-dangerous helper
the original recommendation would have left behind.

**What this still requires of the new code**, because the hole must not simply move:
`sys_getdents64()` calls `ops->readdir` with a **kernel-side `struct vfs_dirent`**, packs into a
**kernel-side buffer**, and hands the result over with **one `copy_to_user()`** at the end. No
driver ever sees a user pointer. `sys_read` already establishes the pattern (`file.c:147`, a
`COPY_BUFFER_SIZE` bounce buffer in a loop).

### 3. The Linux shape ✅ RESOLVED

> **The real `struct linux_dirent64`, exactly as Linux defines it — no PrOS-shaped variant.**

The whole point of the Linux ABI strategy is that off-the-shelf software finds what it expects;
a directory entry is the first structure in this project where the ABI has a *layout* rather than
just a number, so this is where that commitment actually costs something. All three sub-decisions
below are taken as recommended.

```c
struct linux_dirent64 {
    uint64_t d_ino;     // inode number
    int64_t  d_off;     // opaque cookie: where to resume
    uint16_t d_reclen;  // total size of THIS entry, including padding
    uint8_t  d_type;    // DT_REG, DT_DIR, DT_CHR, …
    char     d_name[];  // NUL-terminated, then padding
};
```

Three sub-decisions, none of them free:

- **`d_reclen` must keep the next entry 8-byte aligned.** The header is 19 bytes, so every entry
  is `round_up(19 + strlen(name) + 1, 8)`. Userland iterates by *adding `d_reclen` to a pointer* —
  get the rounding wrong and the consumer walks into the middle of the next record and reads
  garbage. There is no error, no fault, just nonsense filenames.
- **`d_ino`** is always `0` today (see above). **Recommendation:** pass `node->inode` through
  anyway rather than inventing a value. `0` is honest, `ls` doesn't care, and the day `ramfs`
  starts assigning real inodes this needs no change. Inventing an index-derived fake would be a
  lie that later has to be un-told.
- **`d_type`** maps from the VFS flags: `VFS_FILE` → `DT_REG` (8), `VFS_DIRECTORY` → `DT_DIR` (4),
  `VFS_CHARDEVICE` → `DT_CHR` (2), anything else → `DT_UNKNOWN` (0).

### 4. Syscall numbers, and the return contract ✅ RESOLVED

> **`-EINVAL` when the buffer can't hold even one entry, and the offset rollback is a named thing
> to watch for, not something to discover.**

`getdents64` is **217 on x86_64** and **61 on AArch64** — a normal number on both, unlike
`sys_open`'s AArch64 hack. Signature:

```c
int64_t sys_getdents64(int fd, void *dirp, uint64_t count);
```

Returns **bytes written**, `0` at end of directory, `-errno` on failure. The one case worth
deciding deliberately: **a buffer too small to hold even the first entry returns `-EINVAL`**, not
`0`. Returning `0` would tell the caller "directory finished" when it isn't, and the caller would
silently list nothing — a wrong answer rather than an error, which is the worse of the two.

One consequence to be deliberate about: partway through packing, the entry that doesn't fit must
**not** consume its index. `f->offset` no longer advances in a `sys_readdir` wrapper (decision 1
deleted it), so `sys_getdents64()` owns the index itself — read at `f->offset`, and only commit
the advance once the entry is known to fit. That is strictly easier to get right than the
rollback the old shape would have forced, and it is the reason the ordering matters: **decide
whether the entry fits before you decide you have consumed it.** Getting this wrong drops exactly
one file per buffer-full, which is invisible in a directory with two files and infuriating later.

### 5. How does `psh` get started — and when does the machine stop? ✅ RESOLVED

Two problems in one, because today's answers are the same line of code.

`/bin/init` is hardcoded, and the boot task shuts the machine down on a timer. Both are fine for a
program that prints and exits; both are wrong for a shell.

> **Launching: the `pros.init=` knob. Stopping: the boot task waits until every *other* task is
> `TASK_DEAD`.**

**Launching ✅ — a `pros.init=` kernel command line knob, defaulting to `/bin/init`.**
The `pros.tests` precedent already exists, `limine.conf` is already per-architecture, and keeping
`init` bootable means the Phase 5 acceptance test stays runnable instead of being replaced by
this one. It also prefigures what a real `init` argument means, at roughly ten lines of parsing.
The alternative — `psh` simply *becomes* `/bin/init` — is less machinery and loses the older
demo; worth taking if the cmdline parser turns out to be more than a `strstr`.

**Stopping ✅ — the boot task loops until every task other than itself is `TASK_DEAD`.** Not until
a timer expires, and not watching one specific task either. The fixed timeout goes rather than
grows: a bigger number is a slower version of the same bug, and the whole point of this Step is a
session that lasts as long as the human wants it to.

This is *more* general than watching the shell, and costs nothing extra, because the machinery is
already there:

- The run queue is a **circular list through `task->next`**, and
  [`task_dump_all()`](../kernel/src/proc/task.c) (`task.c:153`) already walks it exactly this way
  — start at `sched_get_current_task()`, follow `next`, stop on return. The predicate is that same
  walk asking `state != TASK_DEAD`.
- **Dead tasks are never reaped** — Phase 5 named that as a leak, and `sched_pick_next()` merely
  skips them. Here it works in our favour: the ring never shrinks, the walk always terminates, and
  an exited task stays visible as dead instead of vanishing mid-iteration.
- It is **already the right question for Phase 7**, when `psh` forks children. Watching one
  specific task would need rewriting the moment there are two.

**Where it lives:** in `sched.c`, not `main.c` — `run_queue_head` and the `next` links are
scheduler state and `main()` has no business walking them. One exported predicate:

```c
// true once every task but the caller is dead, nothing left to run for
bool sched_others_all_dead(void);
```

**Two assumptions this makes load-bearing**, worth a comment at the site rather than a surprise
later:

- **BOOT must never die.** `sched_pick_next()` has `kpanic("no runnable tasks")` for the all-dead
  case. BOOT never exits today, so this holds — but it is now shutdown's invariant, not an
  incidental property.
- **The question being asked is really "is there any *user* work left?"** If a later phase adds a
  permanent kernel worker thread that never exits, "every other task is dead" never becomes true
  and the machine never shuts down. Counting every non-self task is correct today and simplest;
  say in the comment what it's actually asking, so the day the assumption breaks, it breaks
  visibly.

*Noted in passing, not scope here:* this makes BOOT explicitly the idle task, which it already is
in practice. Phase 7's natural follow-up is idling on `wfi`/`hlt` rather than busy-spinning —
`arch_halt()` is **not** reusable as-is, since it masks interrupts and loops forever.

### 6. Where do userland's string helpers live? ✅ RESOLVED

> **A new `user/include/string.h`, with `strncmp` rather than `strcmp`.**

`psh` needs at minimum `strlen`, `strncmp`, and a way to split a line on spaces.

**`strncmp`, out of habit — and the habit is right here even though `strcmp` would do.** The
tokenizer NUL-terminates every token, so the two are equivalent for this use; taking the bounded
one anyway costs one argument and means the day a token comes from somewhere less trustworthy —
a file, a script, an `argv` — the comparison is already safe. Same instinct that keeps
`strcpy`/`strncpy` out of the kernel entirely.

One detail worth getting right rather than rediscovering: compare with **`sizeof` the literal**,
not `strlen` of it — `strncmp(tok, "exit", sizeof("exit"))` includes the terminator in the
comparison, so `exit` doesn't match `exitfoo`. Bounding to `strlen("exit")` would.

*The reasoning behind the location, unchanged:* `static inline`, same shape and same rules as
`user/include/syscall.h` — **no `#include` of anything under `kernel/`**, ever, per the boundary
rule that governs `syscall.h` today. Duplicating five lines of `strcmp` is the correct price for
that boundary; a shared header would make `user/` depend on kernel internals to compile.

Worth deciding at the same time: **`strcmp` or `strncmp`?** The lines `psh` compares are
NUL-terminated by its own tokenizer, so `strcmp` is sufficient and simpler. Note that the kernel
deliberately omits `strcpy`/`strncpy` (README) for a reason that applies here too — don't add a
userland version of a function the kernel rejected on principle.

---

## 🗺️ Suggested order

```
  C0 ── sys_getdents64 in the kernel: pack linux_dirent64, one copy_to_user, wire the table
  C1 ── userland plumbing: the getdents64 wrapper + user/include/string.h
  C2 ── psh, launchable and alive: prompt, read a line, echo it back, `exit` ends the session
  C3 ── the easy builtins: help, echo, cat
  C4 ── ls — the one that needs C0, and the acceptance test
```

`C0` first because it's the only kernel work and it's independently testable — a VFS self-test can
call it with a kernel buffer before any shell exists. `C2` before the builtins deliberately: a
shell that prompts, reads and exits is the thing worth having early, because every later Part is
debugged *through* it. `C4` last because `ls` is the only builtin that exercises `C0`, which makes
it the acceptance test for the whole Step rather than just another command.

---

## 🧩 C0 — `sys_getdents64`

**Goal:** a real, Linux-ABI-shaped directory read, reachable from ring 3, with no user pointer
ever touched by a filesystem driver.

```c
// kernel/src/fs/vfs/file.c, replacing sys_readdir
int64_t sys_getdents64(int fd, void *dirp, uint64_t count);
```

- **`sys_readdir()` goes** (decision 1) — from `file.c` and from `core/syscalls.h`. It is the only
  deletion in this Step, and it takes decision 2's hole with it.
- Pack into a kernel buffer; `copy_to_user()` once at the end (decision 2).
- `round_up(19 + strlen(name) + 1, 8)` per entry (decision 3) — and if the first entry alone
  doesn't fit, `-EINVAL` (decision 4).
- **This function now owns `f->offset`**, since no wrapper advances it any more: read at the
  current index, commit the advance only once the entry is known to fit (decision 4).
- `SYS_getdents64` = 217 / 61 in `asm/unistd.h`, one row in `syscall_table`.
- **Rewrite `test_vfs.c`'s readdir loop** against it, keeping the find-`hello.txt`-by-name shape.

**Verify:** the rewritten `test_vfs.c` loop *is* the test — call it with a kernel buffer against
`/root`, walk the returned bytes by `d_reclen`, confirm `hello.txt` comes back and the walk lands
exactly on the end of the returned length. **That last check is the one that catches the alignment
bug**, and nothing else does. Worth a second case with a buffer deliberately sized to force a
split, so the offset-commit ordering is exercised rather than assumed.

---

## 🧩 C1 — Userland plumbing

**Goal:** `psh` can call the syscall and compare a string, without including a single kernel
header.

- `sys_getdents64()` in `user/include/syscall.h`, same hand-written `asm volatile` shape as its
  neighbours.
- `user/include/string.h` — `strlen`, `strncmp`, and whatever the tokenizer needs (decision 6).
- The `struct linux_dirent64` layout, **hardcoded again on the userland side**. This is the
  boundary rule working as intended, not duplication to feel bad about: `user/` states the ABI it
  believes in, and a mismatch with the kernel's is a bug the ABI itself is supposed to make
  visible.

**Verify:** compiles for both architectures. Nothing to run yet.

---

## 🧩 C2 — `psh`, launchable and alive

**Goal:** the shell exists, starts, prompts, reads a line, and can end the session.

- `user/src/psh/psh.c` — the `user/Makefile` auto-discovers `src/*/`, so a new directory is the
  whole build change.
- `pros.init=` (decision 5), and the boot task waiting on `sched_others_all_dead()` rather than
  on the clock — which means the current seconds-counting loop in `main()` goes away entirely,
  along with the periodic `task_dump_all()` unless you want to keep it for its own sake.
- The read loop: write the prompt, `sys_read(0, line, sizeof line)`, remember the line arrives
  **with its `\n`** and `n` includes it.
- **`line` should be at least 256 bytes**, because that's `LDISC_BUF_SIZE` — a smaller buffer means
  one `read()` returns a partial line and the next returns the remainder, which is correct
  behaviour but not what a naive loop expects. Hardcode the 256 with a comment naming where it
  comes from, per the boundary rule.

**Verify:** boot, get a prompt, type a line, see it echoed back by the line discipline, type
`exit`, watch the machine shut down because the shell ended.

---

## 🧩 C3 — The easy builtins

**Goal:** `help`, `echo`, `cat` — everything that needs only what Phase 5 already built.

Tokenize in place: walk the line, replace spaces with `\0`, collect up to a fixed `argv[]` (8 is
plenty). Strip the trailing `\n` first, or every command name will fail to compare. Compare with
`strncmp(argv[0], "echo", sizeof("echo"))` — the `sizeof` includes the terminator, so `echo`
doesn't match `echoes` (decision 6).

`cat` is `open` / `read` / `write(1, …)` / `close` — which is `/bin/init` with a path from the
user instead of a constant. Worth noting how little is new: the whole syscall surface for this
already shipped in Phase 5 Step 2.

**Verify:** `help` lists the builtins, `echo hello world` prints both words, `cat /root/hello.txt`
prints the file, an unknown command says so instead of doing nothing.

---

## 🧩 C4 — `ls`, and the acceptance test

**Goal:** `ls /root` lists `hello.txt`, through `getdents64`, from ring 3.

- `open(path, O_RDONLY)`, loop `getdents64` until it returns `0`, walk each buffer by `d_reclen`,
  print each `d_name`.
- No argument → list `/` (decision: no cwd exists, so there is nothing else it could mean).
- Worth doing even though nothing requires it: mark directories somehow (a trailing `/`), because
  it proves `d_type` survived the packing rather than merely that the names did.

**Verify — boot to a prompt, both architectures:**

```
psh$ ls /
root/
dev/
bin/
psh$ ls /root
hello.txt
psh$ cat /root/hello.txt
HelloWorld
psh$ exit
[K    ] All done here, shutting down.
```

`ls /dev` is the interesting one: `/dev/console` is a mount, not a `ramfs` directory entry, so
whether it appears at all is a real question about how mounts and `readdir` interact — see the
traps below.

---

## 🕳️ Traps worth knowing about in advance

- **`d_reclen` alignment.** Covered in decision 3, repeated here because it is the single most
  likely bug in this Step and it produces garbage rather than an error.
- **The dropped entry at a buffer boundary.** Decision 4's offset-commit ordering. Invisible with
  two files in a directory; guaranteed to bite the first time a directory doesn't fit in one
  buffer. Worth deliberately testing with a buffer sized to force a split, rather than trusting a
  directory small enough never to exercise it.
- **`readdir` does not know about mounts.** `vfs_get_mountpoint()` does longest-prefix matching
  over a flat list; `ramfs`'s `readdir` walks `ramfs`'s own children. `/dev/console` was mounted
  without a `/dev` directory ever existing (Step 2's explicit non-goal), so `ls /dev` may well
  return nothing, or `-ENOENT`, rather than listing `console`. **Decide what it should do before
  discovering what it does** — "mounts are invisible to `readdir`" is a legitimate, nameable
  limitation for this Step, but only if it's named.
- **The trailing `\n` on every line.** It arrives from the line discipline by design (canonical
  mode delivers the newline). Forgetting to strip it means `strcmp("exit\n", "exit")` never
  matches and the shell looks broken in the most confusing possible way.
- **`psh` exiting with nothing left to run.** Once the boot task waits on `sched_others_all_dead()`
  instead of a timer, the shell ending *is* the shutdown path. Make sure it is a shutdown and not a
  hang — a `TASK_DEAD` shell with a boot task still looping is a black screen with no message. The
  first thing to check if that happens is whether the walk starts from the right node: beginning at
  `run_queue_head` rather than at `current` includes BOOT itself, which is never dead, so the
  condition is never true and the symptom is exactly that silent hang.
- **One reader is still an assumption.** `/dev/console`'s `ldisc` is per-node, not per-open
  (Step 2 decision 3), and `read()` spins rather than sleeps (Step 2 decision 4). Both hold
  exactly as long as `psh` is the only thing reading the console — which is true for this entire
  Step and stops being true in Phase 7. Don't let `psh` be the thing that discovers it.

---

## 📁 Files touched

New:

- ⬜ `kernel/include/fs/vfs/dirent.h` (or wherever `struct linux_dirent64` and the `DT_*` constants
  best live) — the ABI shape, kernel side
- ⬜ `user/src/psh/psh.c` — the shell
- ⬜ `user/include/string.h` — minimal freestanding string helpers (decision 6)

Existing, to be modified:

- ⬜ `kernel/src/fs/vfs/file.c` — `sys_getdents64()` added, `sys_readdir()` **removed** (decision 1)
- ⬜ `kernel/include/core/syscalls.h` — `sys_getdents64` declared, `sys_readdir` declaration removed
- ⬜ `kernel/include/asm/unistd.h` — `SYS_getdents64` (217 / 61)
- ⬜ `kernel/src/syscall/syscall.c` — one `syscall_table` row
- ⬜ `kernel/src/core/main.c` — `pros.init=`, and the boot loop waiting on
  `sched_others_all_dead()` instead of the clock (decision 5)
- ⬜ `kernel/src/proc/sched.c` + `kernel/include/proc/sched.h` — `sched_others_all_dead()`
  (decision 5)
- ⬜ `user/include/syscall.h` — the `getdents64` wrapper and the userland `linux_dirent64`
- ⬜ `kernel/src/core/test/test_vfs.c` — its `sys_readdir` loop rewritten against `getdents64`,
  which is where the packing/alignment check lives (decision 1)

---

## 🪜 Verification

- **C0** is testable without a shell — a kernel-buffer self-test that walks the packed bytes and
  lands exactly on the returned length. This is the only Part with a meaningful pure-ish test, and
  it happens to cover the Step's most likely bug.
- **C1** compiles and proves nothing on its own.
- **C2, C3, C4** are only observable by booting and typing — which is the entire point of the
  Phase, and the first time that sentence has been true.

When `C4` passes on both architectures, **Phase 6 is complete**: its three working documents move
to [`archive/`](archive/), `ROADMAP.md`'s Phase 6 entry keeps its numbered Steps heading per the
phase-list lifecycle, and Phase 7 — which now inherits preemptible syscalls, a spinlock, and two
named stopgaps in `/dev/console` waiting for a wait queue — is next.
