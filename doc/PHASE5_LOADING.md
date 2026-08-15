# Working Document: Phase 5 — Loading: The ELF Loader and a Real `/bin/init` [STATUS: NOT STARTED ⬜]

> [!NOTE]
> **Where this stands.** Not started, and blocked on Phase 4 — there is no point loading a
> program until there is somewhere unprivileged to run it and a way for it to call back in.
>
> This design was written as part of the original single-document Phase 3 plan and split out
> when that phase was broken into three along the capability boundaries it had already
> identified. The starting-line inventory lives in
> [`PHASE3_PREEMPTION.md`](archive/PHASE3_PREEMPTION.md) Chapter 0; the privilege drop and the syscall
> entry path are [`PHASE4_PRIVILEGE.md`](PHASE4_PRIVILEGE.md).

> [!NOTE]
> Mentor-mode reminder (see the note at the top of [`../README.md`](../README.md)): this is a
> design reference to code from by hand, not code to paste in. Snippets are illustrative and
> deliberately incomplete.

---

## 🎯 What "done" looks like

> `/bin/init` — a real ELF64 binary you compiled, living in the initrd — is loaded from the
> ramfs into a fresh address space, entered at ring 3 / EL0, opens `/root/hello.txt`, prints
> its contents, and exits.

That single sentence exercises the whole stack: the loader, the address space, the privilege
transition, the syscall path, the fd table, and the filesystem. It is the first moment PrOS is
running software rather than running *itself*.

---

## 🧭 The capability this phase adds: loading

Phase 4's user program was a hand-assembled blob compiled into the kernel. That proves the
privilege boundary works, and nothing else — the program is still something the kernel author
placed there by hand.

This phase makes the thing that runs come from **a file**, which brings two problems that
didn't exist before:

- **The program's memory layout is described by data, not by code.** A loader has to read a
  format and build an address space from it, into a context that isn't currently live.
- **The program's arguments are now hostile.** A pointer arriving in a register from a program
  loaded off disk cannot be dereferenced on faith.

| Term | One-line version | Chapter |
|---|---|---|
| **ELF `PT_LOAD`** | The only ELF program-header type a loader must understand to run a static binary. | 1 |
| **`p_filesz` vs `p_memsz`** | The difference between the two is `.bss`, and the loader must zero it. | 1 |
| **auxv** | The key/value array on the initial user stack that libc reads before `main`. | 1 |
| **`copy_from_user`** | Never trust a pointer that came from userland; validate before dereferencing. | 2 |
| **`-errno`** | The Linux ABI's error convention: negative return *is* the error code. | 2 |

---

## 🏗️ Chapter 1: The ELF loader and the initial user stack

### The minimum viable loader

For a *static, non-PIE* ELF64 executable, the loader needs to understand exactly two
structures: the header (`Elf64_Ehdr`) and the program headers (`Elf64_Phdr`). Section headers,
the symbol table, and relocations are all irrelevant to execution — they're linker and
debugger concerns.

The algorithm:

- Validate `e_ident` magic (`\x7fELF`), class (64-bit), and `e_machine` against the running
  architecture. Rejecting an x86 binary on ARM with a clear message beats debugging a fault
  in fabricated user code.
- For each program header with `p_type == PT_LOAD`:
  - allocate and map `[p_vaddr, p_vaddr + p_memsz)` into the new context, page-aligned
    outward at both ends,
  - copy `p_filesz` bytes from the file at `p_offset`,
  - **zero the remaining `p_memsz - p_filesz` bytes.** That difference is `.bss`. Skip it and
    the program starts with garbage globals — which usually *looks* like working code until
    it doesn't.
  - translate `p_flags` (`PF_R`/`PF_W`/`PF_X`) into `VMM_USER | VMM_WRITABLE | VMM_NO_EXECUTE`.
- `e_entry` is the initial `rip`/`elr`.

### The non-obvious problem: you are writing into a foreign address space

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
the same trick that will be needed for `fork`'s copy-on-write in Phase 7. Note the ordering
benefit: a read-only segment can be genuinely read-only from the moment it's mapped.

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
header location, the entry point, a seed for stack-protector randomness. A freestanding `init`
needs none of it — terminating with `AT_NULL` alone is fine. The real libc in **Phase 9** will
need at least `AT_PAGESZ`, `AT_PHDR`, `AT_PHNUM`, `AT_PHENT`, `AT_ENTRY`, and `AT_RANDOM`.
Building the array as a small table from the start, even with two entries, makes that a data
change rather than a redesign.

The 16-byte alignment is not optional on x86-64: SSE instructions in libc assume it, and the
resulting `#GP` on a `movaps` is one of the classic first-userland-process bugs.

### Getting a test binary without a libc

`zig cc` is already the toolchain (`arch/*/config.mk`), and it can target
`x86_64-linux-none` / `aarch64-linux-none` freestanding. So the first `/bin/init` can be a
~30-line C file with inline-asm syscall wrappers, no libc, statically linked — added to
`initrd/` and picked up by the existing `initrd.tar` rule in the top-level `Makefile`.

That matters for sequencing: **userland can be tested long before a C library exists** — which
is the whole reason Phase 9 can be deferred until after there is an interactive shell.

---

## 🏗️ Chapter 2: The syscall implementations — what changes in existing code

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

Worth being honest about the timing: with one process a global table works fine, so this is
**structural preparation, not a fix for a live bug**. It lands here rather than in Phase 7
because `fork` duplicating a table is a much smaller change than `fork` inventing one.

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

[`SYSCALL_DESIGN.md`](SYSCALL_DESIGN.md) §4 commits to it, current code returns `-1`
everywhere. This is mechanical but touches every function in `file.c`, and it needs
`include/errno.h` to exist. Do it in one pass rather than mixing conventions — a
half-converted error space is worse than either convention alone.

Also worth noting: `sys_readdir` (`include/core/syscalls.h:22`) is **not** a Linux syscall.
Linux uses `getdents64` with a packed variable-length `struct linux_dirent64`. It can stay as
an internal helper, but the ABI-facing syscall will need to be `getdents64` before Phase 6's
`ls` builtin or Phase 9's BusyBox works. Worth knowing it's a rename-plus-reformat waiting.

---

## 🪜 Chapter 3: The Steps

**⬜ Step 1 — The ELF loader and a freestanding `/bin/init`.**
Loader per Chapter 1, plus an `init.c` built by `zig cc` and added to `initrd.tar`. Acceptance:
`/bin/init` loads from the ramfs and prints from ring 3 using only `write` — the one syscall
Phase 4 wired.

**⬜ Step 2 — The real syscall surface.**
Per-process fd tables, `copy_from_user`/`copy_to_user`, the `-errno` conversion, and
`open`/`read`/`close`/`exit`/`getpid` reachable from userland. Acceptance: an init that opens
`/root/hello.txt` and prints its contents — the whole stack, ring 3 to ramfs and back.
**Phase 5's stated goal is met here.**

---

## ⚖️ Chapter 4: Design decisions to make deliberately

| Decision | Options | Recommendation |
|---|---|---|
| ELF segment copy | switch context vs copy via HHDM | HHDM — no context switch, permissions applied last |
| User pointer validation | bounds-check only vs verify mappings | Bounds-check; let a genuine fault be a fault |
| `sys_readdir` | keep vs rename to `getdents64` | Keep as an internal helper now, expose `getdents64` in Phase 6 |
| auxv | omit vs build a table | Build the table with two entries — Phase 9 then needs a data change, not a redesign |

---

## 🕳️ Chapter 5: Traps worth knowing about in advance

- **`p_memsz` vs `p_filesz`** → uninitialised globals; code that works until it doesn't.
- **User stack not 16-byte aligned** → `#GP` on the first `movaps` inside libc, far from the
  cause. Bites in Phase 9, caused here.
- **Copying to `p_vaddr` in the wrong address space** → silent corruption of whatever the
  *current* context has mapped there, or a fault that looks like a loader bug.
- **Two `PT_LOAD` segments sharing a page** → the second allocation replaces the first
  segment's tail with zeros.
- **A half-converted `-errno` space** → callers that check `< 0` and callers that check `== -1`
  in the same tree, disagreeing about what `-1` means.

---

## 📁 Critical files

Existing, to be modified:

- ⬜ `kernel/fs/vfs/file.c` + `kernel/include/fs/vfs/file.h` — fd table into `struct task`,
  `-errno` throughout
- ⬜ `kernel/proc/task.c` — the per-process fd array
- ⬜ `kernel/core/main.c` — load and start `/bin/init` instead of shutting down
- ⬜ `Makefile` (top level) — a rule building `initrd/bin/init` with `zig cc`

New:

- ⬜ `kernel/proc/elf.c` — the loader and the initial-stack builder
- ⬜ `kernel/include/errno.h`
- ⬜ `initrd/bin/init.c` — freestanding, inline-asm syscall wrappers, no libc

---

## 🪜 Verification

This is the first phase since Phase 2 with genuinely unit-testable pieces, because a loader is
mostly a pure function of bytes. Worth adding to `core/test/`:

- **ELF header validation** — accept a good header, reject bad magic, wrong class, and the
  other architecture's `e_machine`.
- **The `copy_from_user` bounds check**, including the wraparound case (`base + len` overflowing
  past `VMM_ADDR_SPLIT`).
- **fd table allocation and exhaustion.**
- **The initial-stack builder's layout and alignment** given a known `argv`/`envp`.

Only observable by booting: the loader actually producing a running process. Expected output
per Step is in Chapter 3.
