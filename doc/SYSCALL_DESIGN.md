# PrOS Syscall Design Reference

> Purpose: design principles for implementing syscalls in PrOS so that porting
> Linux userland software (BusyBox, Newlib/mlibc, etc.) later on stays
> feasible. Keep this file up to date as decisions are made — it's meant to
> be read by both humans and Claude when we design Phase 3+ together.

---

## 1. Core Principle

Match Linux at the ABI boundary (syscall numbers, calling convention, error
codes, struct layouts) even though the kernel internals are entirely custom.
The kernel implementation is ours; the *contract* userland code expects is
Linux's.

---

## 2. Syscall Numbering

- Use Linux syscall numbers, not a custom scheme.
- Sources of truth:
  - x86_64: `arch/x86/entry/syscalls/syscall_64.tbl` (Linux source)
  - AArch64: `arch/arm64/include/asm/unistd.h` (Linux source)
- Store as `#define SYS_xxx N` in `kernel/include/asm/unistd.h`.
- Dispatch via a function pointer table indexed by syscall number.

```c
syscall_handler_t syscall_table[NR_SYSCALLS] = {
  [SYS_read]  = sys_read,
  [SYS_write] = sys_write,
  [SYS_open]  = sys_open,
  // ...
};
```

---

## 3. Calling Convention

### x86_64 (System V AMD64 + Linux syscall convention)
- Args in order: `rdi, rsi, rdx, r10, r8, r9` (note: **r10**, not `rcx`,
  because `syscall` clobbers `rcx`)
- Syscall number in `rax`
- Return value in `rax`
- Entry: `syscall` instruction → return: `sysret`
- (`int 0x80` legacy path also uses `rcx` normally — pick one entry
  mechanism and be consistent; prefer `syscall`/`sysret`)

### AArch64
- Args in order: `x0–x5`
- Syscall number in `x8`
- Return value in `x0`
- Entry: `svc #0` → return: `eret`

---

## 4. Return Value Convention

Return `-errno` on failure, not a separate error flag/out-param.

```c
if (something_fails)
  return -ENOENT;
return actual_result; // >= 0 on success
```

This matches what libc (Newlib/mlibc/glibc) expects:

```c
long result = syscall(SYS_read, ...);
if (result < 0) {
  errno = -result;
  return -1;
}
return result;
```

Use standard Linux `errno` values (`ENOENT=2`, `EBADF=9`, `EAGAIN=11`,
`ENOMEM=12`, `EACCES=13`, `EINVAL=22`, etc.) — defined in
`kernel/include/errno.h`.

---

## 5. Struct Layout Compatibility

Structs that cross the syscall boundary must match Linux's layout
(field order, sizes, padding) for the target architecture, since compiled
userland code reads/writes them directly.

Must match:
- `struct stat` (`<sys/stat.h>`)
- `struct dirent` (`<dirent.h>`)
- `struct timespec` (`<time.h>`)
- `struct sigaction` (`<signal.h>`)

Flags/constants that must match Linux's numeric values:
- `O_RDONLY=0, O_WRONLY=1, O_RDWR=2, O_CREAT=0x40, O_EXCL=0x80,
  O_TRUNC=0x200, O_APPEND=0x400` (`<fcntl.h>`)
- `S_IRUSR=0400, S_IWUSR=0200, S_IXUSR=0100, ...` (`<sys/stat.h>`)
- `SIGTERM=15, SIGKILL=9, SIGCHLD=17, ...` (`<signal.h>`)

Process: pull the exact layout from Linux kernel/glibc headers, adapt types
to PrOS's freestanding type definitions, verify field offsets on both
architectures.

---

## 6. Syscall Semantics (POSIX Behavior)

Implement POSIX-specified behavior, not incidental Linux quirks. Example
for `open()`:
- Returns a non-negative fd on success
- `O_CREAT` + nonexistent path → create it
- `O_CREAT | O_EXCL` + existing path → fail `-EEXIST`
- Respect `O_RDONLY/O_WRONLY/O_RDWR/O_APPEND`
- Respect `mode` permission bits
- Nonexistent path without `O_CREAT` → `-ENOENT`
- No permission → `-EACCES`

---

## 7. Implementation Structure

Keep the architecture-specific entry trampoline in assembly, dispatch and
logic in C.

```asm
; arch/x86_64/syscall_entry.s
syscall_entry:
  ; args already in rdi, rsi, rdx, r10, r8, r9 (per syscall ABI)
  ; syscall number in rax
  cmp rax, NR_SYSCALLS
  jge syscall_not_found
  lea r12, [syscall_table]
  call [r12 + rax*8]
  sysret
```

```c
// kernel/syscalls/read.c
long sys_read(int fd, void *buf, size_t count) {
  if (fd < 0 || fd >= MAX_FDS) return -EBADF;
  struct file *f = current->fds[fd];
  if (!f) return -EBADF;
  // ... VFS read logic
  return bytes_read; // or -errno
}
```

---

## 8. Minimal Header Set Needed

Freestanding headers (from the Zig/compiler toolchain, not reimplemented):
`stdint.h`, `stddef.h`, `limits.h`, `stdbool.h`, `string.h`.

POSIX/syscall headers PrOS must provide (`kernel/include/`):

```
kernel/include/
├── sys/
│   ├── types.h     (pid_t, uid_t, gid_t, off_t, size_t, ssize_t, mode_t, ...)
│   └── stat.h      (struct stat, S_IRUSR etc.)
├── fcntl.h         (O_RDONLY, O_CREAT, ...)
├── unistd.h        (STDIN_FILENO, syscall prototypes)
├── errno.h         (ENOENT, EBADF, ...)
├── signal.h        (SIGTERM, struct sigaction, ...)
└── asm/
    ├── unistd.h      (SYS_* numbers, arch-specific)
    └── sigcontext.h  (register/signal context struct, arch-specific)
```

Rough size: ~600 lines total across all of these. Don't reimplement
`stdint.h`/`stddef.h`/`string.h` — the compiler toolchain already provides
them.

Compiler flags to make these authoritative:
```makefile
CFLAGS += -I$(KERNEL_ROOT)/include -nostdinc -ffreestanding
```

When Newlib/mlibc is ported later (Phase 4), the libc brings its own fuller
header set (`stdio.h`, `stdlib.h`, etc.) and includes/wraps these kernel
headers underneath — these kernel headers become the base layer, not a
throwaway.

---

## 9. Minimum Viable Syscall Set (Phase 3)

Must have:
- `read, write, open, close`
- `exit, exit_group`
- `mmap, brk, munmap, mprotect`
- `rt_sigaction, rt_sigprocmask`
- `fork, execve, wait4`
- `stat, fstat, lstat`
- `getpid, getuid, getgid, geteuid, getegid`

Defer until actually needed by a ported app:
- Networking (`socket` family)
- File-specific `ioctl`
- Advanced process control

Add syscalls incrementally as real ported apps (BusyBox first) demand them —
don't pre-build the entire Linux syscall surface.

---

## 10. Documentation Pattern Per Syscall

```c
/**
 * sys_read: Read from a file descriptor
 *
 * Arguments:
 *   fd    - File descriptor (from open())
 *   buf   - Kernel-space destination buffer
 *   count - Number of bytes to read
 *
 * Returns:
 *   On success: Number of bytes read (0..count)
 *   On error: -errno
 *     -EBADF:  Invalid file descriptor
 *     -EFAULT: buf points to invalid memory
 *
 * Linux reference: man 2 read
 */
```

---

## 11. Testing Strategy

Build a small userland test binary (compiled and loaded as `/bin/init` or a
standalone initrd binary) that exercises each syscall as it's implemented:
write, open/close, mmap/brk, fork/execve, signals. Run under QEMU, verify
against expected Linux semantics before moving to the next syscall.

---

## 12. Why This Matters

Following this discipline:
- BusyBox and standard Unix tools compile/run with minimal patching
- Any Linux-experienced dev can reason about PrOS's syscall ABI directly
- Linux source/man pages become a usable reference for behavior questions
- Debugging is tractable (matches known ABI, not a bespoke one)

Ignoring it:
- Ported apps segfault or fail in confusing ways
- Every syscall bug becomes a bespoke investigation
- Porting effort balloons instead of shrinking over time

---

## 13. Open Design Decisions (fill in as decided)

- [ ] x86_64 entry mechanism: `syscall`/`sysret` only, or also support
      `int 0x80`?
- [ ] Exact struct stat layout finalized for both architectures?
- [ ] Signal delivery mechanism (trampoline / sigreturn) design
- [ ] First real ported app to validate against (BusyBox applet?)
