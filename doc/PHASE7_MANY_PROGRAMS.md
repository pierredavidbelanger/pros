# Working Document: Phase 7 — Many Programs: `fork`, `exec`, Pipes & Signals [STATUS: IN DESIGN 🚧]

> [!NOTE]
> **Phase 7**, from [`ROADMAP.md`](ROADMAP.md), written against the tree as it stands the day
> Phase 6 closed. Everything below marked "confirmed" was read out of the code, not remembered.
> The Steps in Chapter 7 are a proposal; each gets its own document when it starts.

> [!NOTE]
> Mentor-mode reminder (see the note at the top of [`../README.md`](../README.md)): this is a
> design reference to code from by hand, not code to paste in. **The decisions in this document
> are deliberately left open** — they are the interesting part, and resolving them is the work
> that precedes the code. **Decisions 1 and 2 are resolved** (2026-08-25) — they were the phase's
> hinge, and Step 1 cannot be designed without them. The remaining eight belong to Steps 2-5 and
> are deliberately left for when there is built code to decide against.

---

## 🎯 What "done" looks like

> `psh$ ls /root | cat` runs **two separate programs**, connected by a pipe, and prints the
> result. `Ctrl+C` kills a program that isn't listening. A program that ends is *reaped* by its
> parent, not left as a corpse in the run queue forever.

Phase 6 made PrOS interactive. Phase 7 makes it a system that runs *programs*, plural — which is
the first time the word "process" earns its meaning, and the first time PrOS has to answer
questions it has been able to dodge for six phases: what happens when a program waits, what
happens when it dies, and who is allowed to notice.

**What is explicitly *not* in this phase:**

- ❌ **No users, permissions, or `uid`.** Every process is root and nobody checks. That's not a
  gap this phase should close — it's a gap that only means something once there is a login.
- ❌ **No job control** beyond what `Ctrl+C` needs. `bg`/`fg`/`&` and full process groups are a
  Phase 10 concern, alongside PTYs.
- ❌ **No `PATH`, cwd, quoting, or redirection.** `psh` still takes absolute paths. `|` is the one
  piece of syntax this phase adds, because it is the one that proves two processes are real.
- ❌ **No `mmap`.** `fork` will want it eventually; Phase 8 is where it lands.

---

## 🧭 The capability this phase adds: more than one program at a time

Every phase so far has had exactly one interesting user task. Even Phase 6's payoff is a single
process holding the console for its whole life, with `BOOT` spinning next to it. Three problems
appear the moment that stops being true, and none of them exist in the tree today:

- **A program has to be able to wait without burning the CPU.** `/dev/console`'s `read()` spins.
  With one reader that is merely wasteful; with two programs it is a scheduler that spends half
  its time running a loop whose entire job is to notice nothing has happened.
- **A program has to be able to become a *different* program.** `elf_load()` builds an address
  space from nothing today. `execve` has to replace a *running* process's address space while
  standing in it — the same "you are writing into a foreign address space" problem Phase 5
  Chapter 1 solved, except the foreign address space is your own.
- **A program that dies has to be noticed.** Dead tasks are never reaped. Phase 5 called that a
  leak; Phase 6 Step 3 quietly made it load-bearing (see Chapter 3). Both cannot stay true.

| Term | One-line version | Chapter |
|---|---|---|
| **`switch_to`** | Swapping *kernel stacks*, so a task can block mid-syscall and resume there. | 1 |
| **Channel** | Any pointer used as a token: `sleep(chan)` parks on it, `wakeup(chan)` frees it. | 1 |
| **Wait queue** | The heavier alternative — a real list per waitable thing. What `poll` needs. | 1 |
| **`fork`** | One process becomes two, identical except for the return value. | 2 |
| **Copy-on-write** | Share pages until someone writes, then split — `fork` without the copy. | 2 |
| **Zombie** | A dead process kept only so its parent can read its exit code. | 3 |
| **`execve`** | Replace the calling process's program, keeping its pid and its fds. | 3 |
| **Pipe** | An in-kernel buffer with a read end and a write end, each a real fd. | 4 |
| **Signal** | An asynchronous event delivered by hijacking the user's own stack. | 5 |

---

## 🏗️ Chapter 1: Blocking — the `switch_to` that does not exist

### Where this actually stands

Confirmed by reading [`sched.c`](../kernel/src/proc/sched.c) and
[`task.h`](../kernel/include/proc/task.h):

- **There is exactly one way to switch tasks, and it is `sched_on_trap_exit()`.** The timer sets
  `need_resched`; the trap-exit path picks a different task and returns *its* frame instead of the
  one it was handed. Phase 3 called this "preempted at trap exit rather than preemptible", and it
  is the reason the scheduler is four lines of assembly instead of a context-switch routine.
- **`sched_pick_next()` skips `TASK_DEAD` and panics on `no runnable tasks`.** There are three
  states: `TASK_DEAD`, `TASK_READY`, `TASK_RUNNING`. Nothing means "waiting".
- **`struct spinlock` exists** (irqsave-only), and **syscalls are preemptible** — both landed
  early in Phase 6 Step 2. This chapter is the thing they were groundwork *for*.
- **`console_dev_ops_read()` spins** on `ldisc_ready()` with interrupts on, and says so in a
  comment. **The `ldisc` is per-node**, in `node->priv_data`, not per-open.

### The problem, stated exactly

A trap-exit swap works because a task at trap exit has *nothing live on its kernel stack*. A task
that wants to block in the middle of `sys_read` does: C frames, locals, a half-finished call. To
resume it later you must preserve and restore the **kernel** stack pointer and the callee-saved
registers — which is a `switch_to`, and is precisely what Phase 3 deliberately did not build.

So there is no way to bolt blocking onto the current scheduler without either building that, or
changing what "blocking" means.

### ⚖️ Decision 1 — how does a task block? ✅ RESOLVED (2026-08-25)

> **A real `switch_to`, subsuming the trap-exit swap rather than sitting beside it. xv6's shape —
> a dedicated scheduler thread — and `BOOT` is it.**

**First, a distinction the original framing of this decision blurred: `switch_to` is a
*mechanism*, `sleep`/`wakeup` is a *policy* on top of it.** They are different layers, and every
kernel worth copying has both. Decision 1 is the mechanism; Decision 2 is the policy.

#### What `switch_to` actually is

PrOS today swaps a **trap frame**. `switch_to` swaps a **kernel stack**:

```
push callee-saved regs onto A's kernel stack
save A's sp into A->context
load  B's sp from   B->context
pop callee-saved regs off B's kernel stack
ret            <- returns into B's C code, wherever B called switch_to from
```

That `ret` is the whole trick. B is not *started* — it **returns** from the `switch_to` it called
minutes ago, with its locals, its call stack and its half-finished `sys_read` intact. Which is
exactly the property the trap-frame swap cannot give, and it makes blocking ordinary C:

```c
p->state = TASK_BLOCKED;
sched();              // returns later, same stack, same frame, same locals
```

#### Why the "two switching paths" objection was wrong

The first version of this decision counted `switch_to` as *a second, entirely separate
context-switch path* next to the trap-exit swap. **Linux says that is a false choice.** There, the
timer does not switch anything — it sets `TIF_NEED_RESCHED`, and the trap-exit path *calls*
`schedule()`, which calls `switch_to`. One mechanism, two callers.

The same applies here: `sched_on_trap_exit()` becomes a **caller** of the switch rather than a
peer to it. And once tasks switch by swapping stacks, the return-a-different-frame trick is not
needed at all — each task returns through its own trap exit naturally.

#### The expensive half is already paid for

**Phase 3 Step 2 gave every task its own 16 KiB kernel stack with a guard value.** That is
precisely what makes `switch_to` a dozen instructions instead of an architecture. PrOS built the
hard prerequisite two phases early and has never cashed it in.

#### How the field actually does it

- **Linux** — `__switch_to_asm` on x86_64: pushes `rbx`/`rbp`/`r12`-`r15`, swaps `rsp` into
  `task_struct->thread.sp`. Blocking is `set_current_state()` + `add_wait_queue()` + `schedule()`.
  Linux *also* has `-ERESTARTSYS`, but **for signal semantics, never as the blocking mechanism** —
  a signal interrupts a sleeping syscall, the handler runs, and the call restarts if `SA_RESTART`.
- **xv6** — `swtch(&p->context, &mycpu()->context)`, the same dozen instructions. Note the shape:
  xv6 always goes **process → scheduler → process** (two switches, a per-CPU scheduler thread),
  where Linux goes process → process directly. **xv6's shape is the one taken** — see below.
- **SerenityOS, ToaruOS** — conventional context switch, more machinery around it.
- **MINIX 3** — genuinely different, and not a scheduler choice: a microkernel where drivers are
  processes and "blocking" is waiting for a message, so the kernel never blocks mid-call because
  it never does long work. A whole-system architecture, noted for the trail rather than as an
  option.

#### `BOOT` is the scheduler thread — and leaves the run queue

**xv6's scheduler thread is not a schedulable entity.** It is the thing outside the run queue that
picks from it; were it in the queue it would pick itself. So `BOOT` becomes it, and **stops being a
candidate in `sched_pick_next()`** — while staying a `struct task`, because it still needs its
16 KiB kernel stack, its guard value, and its line in `task_dump_all()`.

That is the shape `main()` already has. It sets up, goes preemptive, and today falls into a spin
loop; in the new shape it sets up and falls into `scheduler()` forever, which is xv6's `main()`
exactly.

**This is why the shape was chosen, and it pays off on a trap named in Chapter 7.** Once tasks can
block, `sched_pick_next()`'s `kpanic("no runnable tasks")` is wrong — "everything is blocked" is
just an idle machine. A scheduler loop tells the two cases apart trivially, where a ring walk
cannot:

```
nothing runnable, some still alive  → idle: wfi/hlt, and go round again
no live tasks at all                → shut down
```

So **`sched_only_current_is_alive()` is not rewritten, it is absorbed**: the shutdown check becomes
the scheduler loop's own termination condition. That also defuses much of the reaping-versus-
shutdown collision in Chapter 3 — the loop is asking about *live tasks*, not about the shape of a
list that reaping is busy mutating.

One consequence worth having in mind while writing it: **a task preempted at trap exit is already
at trap exit**, so its kernel stack is nearly empty when it calls `switch_to`. It returns from that
call later and simply continues out to userland. **Preemption and voluntary blocking become the
same code path** — which is xv6's `yield()`, and is the concrete form of "`sched_on_trap_exit()`
becomes a caller rather than a peer".

*Kept for the trail — the two rejected alternatives:*

- **(b) Restartable syscalls.** The blocking syscall returns `-ERESTARTSYS`, the trap path rewinds
  the user PC to the `syscall`/`svc` instruction, and the task re-enters from the top when next
  scheduled. No new switch path — but **it does not actually let a task sleep**, it only stops it
  spinning inside the kernel. `wait4` on a child that runs for a minute would re-enter and re-fail
  thousands of times. A polling design wearing a blocking design's interface, and essentially
  nobody uses it as the primary mechanism.
- **Mach continuations** — the honest middle ground, and the reason it is worth knowing about:
  `thread_block(continuation)` let a blocking thread **discard its kernel stack** and resume at a
  named function instead of a saved stack, adopted to avoid paying a kernel stack per blocked
  thread. Largely abandoned; XNU still carries traces. The lesson the field took from it is that
  saving stack memory was not worth writing every blocking path twice. PrOS has already decided to
  pay for per-task kernel stacks, so the trade it optimizes does not exist here.

### ⚖️ Decision 2 — channels, or real wait queues? ✅ RESOLVED (2026-08-25)

> **xv6-style channels for Phase 7, with `poll`/`select` named in advance as the thing that
> retires them.**

This is Decision 1's policy half — given a task *can* block, what does it block *on*?

**xv6's answer is that there is no wait-queue data structure at all.** A "channel" is any pointer
used as a token, and `wakeup(chan)` scans the process table for anyone sleeping on that address:

```c
sleep(&pipe->nread, &pipe->lock);   // block until someone reads
wakeup(&pipe->nread);               // wake everyone waiting on that token
```

Roughly fifteen lines, O(n) in processes — which here is O(3). FreeBSD's `tsleep`/`msleep` and
Plan 9's `Rendez` are the same idea; xv6 inherits it from v6 Unix.

| | xv6 channels | Linux wait queues |
|---|---|---|
| **What you sleep on** | any pointer, used as a token | a `wait_queue_head_t` you declared |
| **Data structure** | none | a list per waitable thing |
| **Wake** | scan every task for `chan == x` | walk that queue |
| **Wake one?** | no — always broadcast | yes |
| **On many things at once?** | **no** | yes — one entry per queue |
| **Cost** | ~15 lines | ~200 |

**Every wait Phase 7 needs is a single-channel wait**, and xv6 does each of them exactly this way:
`wait4` sleeps on the parent's `struct task *`, a pipe on `&pipe->nread` / `&pipe->nwrite`, the
console on its `ldisc`. Channels also sidestep the run-queue problem entirely — a task cannot be
on two `next`-threaded lists at once, and with channels there is no second list to be on.

**What retires them: `poll`/`select`** — "wake me when *any* of these N fds is ready". A channel
lets a task sleep on one address; `poll` needs it on many, which is what Linux's per-queue list
node exists for. That arrives in **Phase 9-11**: BusyBox applets use `poll`, and Xfbdev's main loop
is a `select` over the X socket and input. Timeouts (`nanosleep`, and Phase 12's socket deadlines)
are the other pressure, though those are additive — a per-task deadline the timer tick checks.

**Why this is a safe bet rather than a corner: `sleep(chan, lk)` / `wakeup(chan)` is an interface,
and the scan is one implementation of it.** The day `poll` needs multi-wait, the process-table
scan is replaced with real queues *under the same two functions* and no caller changes — provided
callers only ever call `sleep`/`wakeup` and never reach into the internals. **Write that rule down
where the functions are declared**, because it is the whole reason the cheap version is allowed.

Two smaller things that arrive sooner and are extensions rather than walls:

- **A signal has to be able to interrupt a sleep** (Step 5). xv6's version is crude — it only
  checks `p->killed`; Linux's interruptible/uninterruptible distinction is what you actually want.
  Foreseeable, and small.
- **Spurious wakeups are normal in both designs.** Every sleeper re-checks its condition in a
  `while`, never an `if`. Linux requires this too, so it is not a channel tax — but a blocking
  path written with an `if` is a bug that only shows up under load.

### ⚖️ Decision 3 — the two `/dev/console` stopgaps ✅ RESOLVED (2026-08-26)

> **The `ldisc` stays per-node — that is where a line discipline belongs. The second stopgap was
> the wrong gap: what is missing is not per-open state, it is *arbitration*. A `struct tty` gathers
> the console's split state, a lock plus Decision 2's channel make concurrent readers safe, and a
> named owner turns "one console reader" from an accident into a rule.**

Both were named in Phase 6 Step 2. The first is unchanged: **`read()` spins** — retired by
Decision 1, in Step 1.

#### Two facts that reframe the second half

Confirmed by reading the tree, which is how this decision's original framing should have been
written and was not:

1. **`struct file` has no `priv_data`.** [`file.h:8`](../kernel/include/fs/vfs/file.h:8) is
   `node`, `offset`, `flags`, `ref_count`, and nothing else. Phase 6 Step 2's decision 3 sent
   per-open state "via the `struct file`'s `priv_data` (already exists…)" — it does not exist;
   only `struct vfs_node` carries one. Corrected in place in that archived document.
2. **Per-open would not separate a `fork`ed pair.** `task_open_std_fds()`
   ([`task.c:18`](../kernel/src/proc/task.c:18)) opens `/dev/console` *once* and `file_ref()`s it
   into fds 0/1/2, and `fork`'s fd-table half is a `file_ref()` loop over `fds[]` — so parent and
   child share one `struct file`, deliberately and permanently. Per-open means per-`open()`-call,
   and nothing in the tree opens the console twice. **The scenario this decision named — "with
   `fork`, two processes really can hold the console" — is precisely the case per-open does not
   cover.**

So the question was never where the line buffer lives. It is **who gets the bytes**, and there is
exactly one queue of them: `console_input`'s ring is a file-scope static
([`console_input.c:9`](../kernel/src/core/console_input.c:9)) while the `ldisc` sits in the node's
`priv_data`. One device's state living in two homes — that is the real smell, and it is not the
one that got written down.

#### What real kernels do here

The line discipline is per-**tty** in Linux and in every Unix it descends from — never per-open.
Two processes reading one tty steal each other's input, and that is documented behaviour rather
than corruption; job control is what stops it happening. **Per-node was the right shape all
along.** It was under-defended, not misplaced.

#### What lands, and where

- **`struct tty`** — the `ldisc`, the input ring, the wait channel, and later the foreground
  process group, in one allocation the console node's `priv_data` points at. **Step 2.** Phase
  10's PTY work then means allocating a second one instead of a rewrite.
- **A lock and one channel** — `struct spinlock` around feed/drain, and every sleeper re-checking
  `ldisc_ready()` in a `while` per Decision 2's spurious-wakeup rule. This makes two readers
  *safe*, not *orderly*: whoever wakes first takes the line. **Step 2**, with `fork`.
- **A named owner** — the first reader claims the console, a second gets `-EIO`. Ten lines, no
  signals, and the assumption becomes a rule that fails loudly. **Step 2.**
- **Job control** — controlling terminal, foreground process group, `SIGTTIN` for a background
  reader (`-EIO` is what Unix gives when that signal cannot be delivered, which is why it is the
  right placeholder). The grown-up owner. **Step 5**, with signals — and if Step 5 is split off,
  the named owner is what stands in its place.

*Kept for the trail — why per-open was rejected:* it needs `priv_data` on `struct file`,
`vfs_ops.open`/`close` taking a `struct file *` rather than a node
([`vfs.h:23`](../kernel/include/fs/vfs/vfs.h:23)), and `read` taking one too
([`vfs.h:27`](../kernel/include/fs/vfs/vfs.h:27)) — which breaks
[`elf.c:74`](../kernel/src/proc/elf.c:74), where `ops->read(node, …)` runs with no open file in
existence at all. Three signature changes across every filesystem, and at the end of it one UART
queue still feeds N line buffers and something still has to decide who gets each byte. It buys a
bigger interface for the same unanswered question, and leaves `fork` uncovered.

---

## 🏗️ Chapter 2: `fork` — one process becomes two

### Where this actually stands

- **`vmm_create_context()` clones the kernel upper half and nothing else**; the lower half starts
  empty. There is **no API that walks or copies a lower-half tree** — `vmm_destroy_context()`
  frees one, so the walk exists in spirit but not as something reusable.
- **The PMM has no per-page reference count.** It is a free list of frames. Copy-on-write needs to
  know when the last sharer is gone, so CoW implies a page refcount, which is a real PMM change,
  not a VMM one.
- **`vmm_handle_page_fault(fault_addr, error_code)` already exists** and already decodes the
  *actual* fault reason for demand paging. A CoW fault has a natural home there, next to a
  mechanism that has been distinguishing legitimate faults from real ones since Phase 1.
- **`struct file` is already reference-counted** — `ref_count`, `file_ref()`, `file_unref()`. The
  fd-table half of `fork` is a `file_ref()` in a loop over `fds[]`, and that is genuinely already
  done.
- **`struct task` has no parent field**, no exit code, and no notion of a child.

### The non-obvious problem: `fork` returns twice

The child does not start at an entry point — it resumes in the middle of a syscall, at the same
user PC as the parent, with `0` in the return register instead of the child's pid. Concretely
that means **the child needs its own kernel stack carrying a copy of the parent's trap frame**,
with one register changed. `task_create_user()` fabricates a first frame by hand (Phase 3's
trick); `fork` does the opposite — it *copies* a frame that the hardware really did write, and
edits one field.

Two details worth having in mind before writing it:

- `task->frame` is documented "valid only while suspended", and `task_owns_frame()` panics if a
  task is handed a frame that isn't on its own kernel stack. **A copied frame must land on the
  child's stack**, at the same offset, or that assertion fires — and it firing is the good case.
- `sched_add_task()` inserts **after `run_queue_head`**, not at the tail. A forked child is
  therefore scheduled sooner than a naive reading of "round robin" suggests. Harmless, but it
  will surprise someone reading a trace.

### ⚖️ Decision 4 — eager copy or copy-on-write? **OPEN**

`ROADMAP.md` says eager first, CoW once it works, and that ordering is almost certainly right —
eager copy is a page-table walk and a `memcpy` per page, and it is *testable* in a way CoW is not
until faults are firing. The decision worth making deliberately is not which one, but **whether
eager copy is written in a shape CoW can grow out of**, or as a throwaway. A `vmm_copy_context()`
that takes a per-page policy callback is one answer; two functions that share nothing is another,
and honestly not a bad one.

### ⚖️ Decision 5 — FPU / SIMD state **OPEN**

This is the phase where it becomes wrong rather than merely absent, because it is the first time
two *user* tasks coexist. Confirmed from the configs:

- **AArch64 is nearly free** — `kernel/config.aarch64.mk` carries `-mgeneral-regs-only`, so the
  kernel emits no FP/SIMD at all and only user state is at stake.
- **x86_64 is not** — `kernel/config.x86_64.mk` has no `-mno-sse`, so the compiler may use SSE in
  kernel code. Until it does not, "save the user's FPU state on switch" is approximately correct
  rather than correct.

So the real decision is *ordering*: add `-mno-sse` first and find out what breaks, or write lazy
FPU switching first and accept that x86_64 is subtly wrong until the flag lands.

---

## 🏗️ Chapter 3: `execve`, `wait4`, and the death of a process

### Where this actually stands

- **`elf_load(path, ctx, out)` takes the context to load into** — it never assumed "the current
  one", which means `execve` can build a fresh context and swap, rather than mutating the one it
  is standing in. That is a Phase 5 decision paying off unprompted.
- **`elf_build_user_stack(ctx, stack_top, argc, argv, envp)` already takes `argc`/`argv`/`envp`.**
  `main.c` passes `0, NULL, NULL`. **The argv plumbing execve needs is already written and has
  never been used.**
- **`sched_exit_current()` marks the task `TASK_DEAD` and sets `need_resched`.** Nothing frees the
  task, its stack, its context, or its fds. Phase 5 named this a leak.
- **Phase 6 Step 3 made that leak load-bearing.** `sched_only_current_is_alive()` walks the
  `next` ring asking "is every task but me dead?", and it terminates *because the ring never
  shrinks*. It is BOOT's shutdown predicate — the thing that ends a `psh` session.

### The collision worth seeing early

**Reaping is the direct enemy of the shutdown predicate.** The moment a dead task is removed from
the run queue, "walk the ring and check every task is dead" is asking a question about a list that
no longer contains the answer. This is not a bug to discover; it is a design consequence to decide
in advance:

**Decision 1 defuses much of this**: with `BOOT` as the scheduler thread, the shutdown check is
the scheduler loop asking "are there any live tasks left?", which is a question about liveness
rather than about the shape of a list reaping is busy mutating. What remains is genuinely about
zombies:

- If reaping removes tasks from the ring, the shutdown condition becomes something like "only
  BOOT remains", which is a *different* predicate with a different failure mode (a leaked,
  never-reaped task keeps the machine alive forever, silently).
- If zombies stay in the ring until `wait4`, then an unwaited-for child keeps `psh` alive after it
  exits — arguably correct Unix behaviour, arguably a hang.

### ⚖️ Decision 6 — who reaps, and what happens to an orphan? **OPEN**

Real Unix reparents orphans to `init`. PrOS has an `init` (`pros.init=`, which may be `/bin/psh`),
so the machinery is nearly there — but the parent of everything is `BOOT`, a kernel thread that
never calls `wait4`. Whether BOOT grows a reaping loop, or orphans are freed immediately, or the
shutdown predicate is what collects them, is one decision with three plausible answers.

### ⚖️ Decision 7 — `stat`/`fstat`, and how big to make it **OPEN**

Phase 6 left `psh` wanting it: `ls <file>` cannot tell a regular file from a directory it failed
to open, and both report `Cannot read`. Linux's `struct stat` is large, architecture-shaped, and
mostly fields PrOS has no answer for (`st_dev`, `st_rdev`, three timestamps, `st_blocks`).
`vfs_node.inode` is still always `0` (Phase 6 Step 3, decision 3).

The same commitment `getdents64` made applies — **the real Linux layout, with honest zeros in the
fields PrOS cannot fill**, rather than a PrOS-shaped struct that BusyBox will not recognize. Worth
confirming rather than assuming, because it is a bigger struct than `linux_dirent64` and the cost
is more visible.

---

## 🏗️ Chapter 4: `pipe`, `dup2`, and `|`

### Where this actually stands

- **`struct file` is `{node, offset, flags, ref_count}`** and is already ref-counted and shared.
  `dup2` is "point two fd slots at one `struct file` and `file_ref()` it" — the structure for that
  already exists and is already exercised, because every task is born with fds `0`/`1`/`2` on the
  same console node.
- **A pipe has no `vfs_node` shape yet.** `/dev/console` is the precedent: a `VFS_CHARDEVICE` with
  custom `vfs_ops` and its state in `priv_data`. A pipe is the same trick with a different buffer
  — except it has *two ends with different rights*, which no existing node does.
- **`ring_buffer` already exists** (`core/ring_buffer.c`), written for UART input. Whether a pipe
  reuses it or wants its own is worth a moment rather than a reflex.

### ⚖️ Decision 8 — is a pipe a `vfs_node`? **OPEN**

Making it one buys `read`/`write`/`close` for free through the existing fd path. It also means an
anonymous, unmounted node — every `vfs_node` so far has lived in a mount table under a path, and
`vfs_get_mountpoint()` does longest-prefix matching over exactly that list. An anonymous node is
not hard; it is just the first one, and "the first one" is where assumptions surface.

The blocking half is Chapter 1's problem arriving in a second place: a read on an empty pipe and a
write to a full one both have to wait, and **a read on a pipe whose write end is closed must
return `0`, not block forever.** That last one is the whole reason `ls | cat` terminates.

---

## 🏗️ Chapter 5: Signals — `Ctrl+C` and the stack you did not push

### Where this actually stands

- **Nothing exists.** No pending mask, no handler table, no delivery.
- **The `ldisc` sees every byte** and already drops control bytes (Step 2 has a self-test named
  `dropped control bytes do not echo`). `0x03` currently goes nowhere, which is exactly the right
  place for it to start going somewhere.
- **`struct task` has no notion of a foreground anything.**

### The non-obvious problem

Delivering a signal means **making a user program call a function it never called**: push a frame
onto its *user* stack, point the trap frame's PC at the handler, and arrange that when the handler
returns it lands in `sigreturn` — which restores the frame that was interrupted. It is the
fabricated-first-frame trick from Phase 3 again, one privilege level down and with a real frame
underneath that must survive intact.

### ⚖️ Decision 9 — how much of a process group? **OPEN**

`Ctrl+C` has to reach "the program in the foreground", and Unix's answer is process groups,
sessions and a controlling terminal — a large amount of machinery for one keystroke. The minimal
honest version is **one foreground task pointer on the console node**, set by whoever last ran a
child. It is not Unix, it is not wrong for this phase, and it is the kind of shortcut worth naming
in the code so Phase 10's PTY work knows what it is replacing rather than discovering it.

### ⚖️ Decision 10 — default actions, or handlers, or both? **OPEN**

`SIGINT` with a default action (kill the target) needs no user stack manipulation at all — it is a
flag checked at trap exit that turns into `sched_exit_current()`. **That is a fraction of the work
and most of the demo.** Full `sigaction` with user handlers and `sigreturn` is the real feature.
Doing the first alone is a legitimate scope for this phase, as long as it is a decision and not an
accident.

---

## 🗺️ Chapter 6: The Steps

A proposal, ordered so each Step is demonstrable and each unblocks the next. Every Step is
architecture-neutral (`C` track) **except Steps 1 and 2**: `switch_to` is per-architecture
assembly (Decision 1), and so is FPU state.

- **⬜ Step 1 — Blocking and wait queues.** Chapter 1. Retires the first `/dev/console` stopgap;
  the console's arbitration — a `struct tty`, a lock, a named owner — waits for Step 2, since
  nothing can contend for it until `fork` exists.
  *Demo:* the console `read()` no longer spins — visible as the shell's `switch_count` staying
  flat while nothing is typed, which `task_dump_all()` already prints. Individually designed in
  [`PHASE7_STEP1_BLOCKING.md`](PHASE7_STEP1_BLOCKING.md).
- **⬜ Step 2 — `fork`, `wait4`, reaping, and FPU state.** Chapters 2 and 3, minus `execve`.
  The first two user processes, and the first process that gets cleaned up. Carries the
  shutdown-predicate rewrite. *Demo:* a program forks, both halves print, the parent waits and
  reports the child's exit code, and the machine still shuts down afterward.
- **⬜ Step 3 — `execve`, plus `stat`/`fstat`.** Chapter 3's other half. *Demo:* `psh` runs
  `/bin/init` as a real child process and gets its prompt back — and `ls /root/hello.txt` finally
  prints the filename.
- **⬜ Step 4 — `pipe`, `dup2`, and `|` in `psh`.** Chapter 4. *Demo:* `ls /root | cat`.
- **⬜ Step 5 — Signals.** Chapter 5. *Demo:* `Ctrl+C` kills a program stuck in a loop and returns
  you to the prompt.

**Where the phase could reasonably stop early:** after Step 4 the payoff sentence is already true
except for `Ctrl+C`. If Step 5 grows past its worth, splitting it out is more honest than
finishing it badly.

---

## 🕳️ Chapter 7: Traps worth knowing about in advance

- **The shutdown predicate and reaping are in direct conflict.** Chapter 3 spells it out. This is
  the single most likely way Phase 7 breaks Phase 6's payoff, and the symptom — a machine that
  never shuts down, or shuts down while a program is still running — will look like a scheduler
  bug rather than a predicate that is asking the wrong question.
- **`task_owns_frame()` will fire on a badly built `fork`, and that is the good outcome.** The
  bad outcome is a child frame that happens to be on the right stack with wrong contents.
- **`fds[MAX_OPEN_FILES]` is 256 pointers, inline in `struct task`.** `fork` copies the whole
  array. That is 2 KiB per process copied on every fork, which is fine — but `struct task` is
  bigger than it looks, and it is `kmalloc`'d.
- **One reader on the console stops being an assumption the instant `fork` lands.** Phase 6 Step 3
  wrote down "don't let `psh` be the thing that discovers it". `fork` is the thing that discovers
  it.
- **`-mno-sse` is a compiler flag that changes what the *kernel* is allowed to do**, not a
  scheduler change. Adding it may surface existing kernel code that quietly used SSE — most likely
  in `memcpy`/`memset` — and that failure will look nothing like an FPU bug.
- **A pipe whose write end is closed must EOF, not block.** Get it wrong and `ls | cat` hangs
  after printing the correct output, which is the most confusing possible way to be nearly right.
- **`sched_pick_next()` panics with `no runnable tasks`.** Once tasks can block, "everything is
  blocked" is a reachable state that is *not* an error — it is what an idle machine looks like.
  **Decision 1 answers this structurally** rather than by invariant: the scheduler loop is where
  you land when nothing is runnable, so there is nothing left to panic about. Listed anyway,
  because it is the first thing that breaks if the scheduler thread is ever made schedulable.

---

## 📁 Critical files

Existing, to be modified:

- ⬜ [`kernel/src/proc/sched.c`](../kernel/src/proc/sched.c) + `proc/sched.h` — blocking, wake-up,
  and whatever replaces `sched_only_current_is_alive()`
- ⬜ [`kernel/src/proc/task.c`](../kernel/src/proc/task.c) + `proc/task.h` — parent, exit code,
  a `TASK_BLOCKED` state, real destruction
- ⬜ [`kernel/src/mm/vmm.c`](../kernel/src/mm/vmm.c) + `mm/vmm.h` — lower-half copy, and the CoW
  hook in `vmm_handle_page_fault()`
- ⬜ [`kernel/src/mm/pmm.c`](../kernel/src/mm/pmm.c) — per-page reference counts, only if CoW
- ⬜ [`kernel/src/proc/elf.c`](../kernel/src/proc/elf.c) — `execve` reusing `elf_load()` and the
  `argv`/`envp` path that already exists
- ⬜ [`kernel/src/drivers/console_dev.c`](../kernel/src/drivers/console_dev.c) — the spin retired,
  and the `ldisc` folded into a `struct tty` with a named owner (Decision 3)
- ⬜ [`kernel/src/syscall/syscall.c`](../kernel/src/syscall/syscall.c) +
  `kernel/include/asm/unistd.h` — one row and one number per new syscall
- ⬜ [`kernel/src/core/main.c`](../kernel/src/core/main.c) — the spin on
  `sched_only_current_is_alive()` becomes a fall-through into `scheduler()`, forever (Decision 1)
- ⬜ [`kernel/config.x86_64.mk`](../kernel/config.x86_64.mk) — `-mno-sse`
- ⬜ [`user/src/psh/psh.c`](../user/src/psh/psh.c) — `fork`/`exec` instead of builtins, and `|`

New:

- ⬜ `switch_to` per architecture — a dozen instructions each, on the per-task kernel stacks
  Phase 3 Step 2 already built (Decision 1)
- ⬜ `sleep(chan, lk)` / `wakeup(chan)` — and the rule, written where they are declared, that
  callers never reach past them (Decision 2)
- ⬜ `struct tty` — the console's `ldisc`, input ring, wait channel and future foreground pgrp in
  one allocation, replacing state split between a node and a file-scope static (Decision 3)
- ⬜ A pipe implementation, and whatever node shape Decision 8 picks
- ⬜ Signal delivery and `sigreturn`, per architecture

---

## 🪜 Verification

- **Step 1** is the only Step whose payoff is an *absence* — no spinning. It needs a deliberate
  way to see that, because "it still works" is what both success and failure look like.
  `task_dump_all()`'s `switch_count` is the existing instrument.
- **Steps 2-5** are each demonstrable from the prompt, which is what Phase 6 bought. From here on,
  a feature that cannot be shown at the `#` prompt should be suspected of not existing.
- **Both architectures, every Step**, as always. Step 2 is the one with real per-architecture
  content and therefore the one where "it works on x86_64" means the least.

When Step 5 lands — or Step 4, if Chapter 5 is split out — **Phase 7 is complete**: this document
and its Step documents move to [`archive/`](archive/), `ROADMAP.md`'s Phase 7 entry keeps its
numbered Steps heading per the phase-list lifecycle, and Phase 8 — `/dev/fb0`, a short phase with
a large visual reward — is next.
