# Working Document: Phase 6 — Talk Back: Serial Input, TTY & a Shell You Wrote [STATUS: NOT STARTED ⬜]

> [!NOTE]
> **Where this stands.** Not started. Phase 5 is complete on both architectures — `init` opens,
> reads, writes, and exits for real — so this Phase is unblocked. Only **Steps 1 and 2** are
> designed here; **Step 3** (`getdents64` + `psh`) is deliberately left undesigned. It depends on
> how Steps 1-2 actually land, and gets its own Chapter and document when it starts, same as every
> other Step in this project.

> [!NOTE]
> Mentor-mode reminder (see the note at the top of [`../README.md`](../README.md)): this is a
> design reference to code from by hand, not code to paste in. Snippets are illustrative and
> deliberately incomplete.

---

## 🎯 What "done" looks like

> You type at your OS over `-serial stdio`, and it answers. A freestanding shell, no libc,
> reads a line you typed, runs a builtin, and prints the result.

That's the whole Phase's payoff — and it needs all three Steps to reach it. Steps 1 and 2, the
ones this document actually designs, don't get there alone: they build the plumbing a shell needs
(a byte reaches the kernel, a line reaches a program) without yet being a shell. Neither Step needs
its own standalone demo the way earlier phases' Steps did — Step 1 is a real building block with
no user-facing behavior of its own; it only matters once Step 2 (and eventually Step 3) use it.

---

## 🧭 The capability this phase adds: input

Every phase so far has been one-directional from the human's point of view — output flows out via
`kprintf`/the console, and the *kernel's own* syscall path already lets a program talk back to the
kernel (Phase 5), but nothing a human types has ever reached PrOS. This phase is the first time a
live keystroke, arriving asynchronously and unrelated to anything the kernel was doing, has to be
caught, buffered, and eventually handed to a program that asked for it.

Two problems that don't exist yet:

- **Input arrives on its own schedule.** A UART byte can show up at any instruction boundary,
  nothing is polling for it — it has to be an interrupt, feeding a queue something else drains
  later. This is the same shape as the timer, just producer/consumer instead of periodic.
- **A raw byte stream isn't a line.** A program calling `read()` on a keyboard wants a complete,
  editable line — backspace has to actually erase the previous character, not just insert a `0x08`
  into a buffer. That's a real state machine (a *line discipline*), not just plumbing.

| Term | One-line version | Chapter |
|---|---|---|
| **UART RX interrupt** | The hardware tells the CPU a byte arrived, instead of the CPU asking. | 1 |
| **Ring buffer** | Fixed-size producer/consumer queue between the ISR and whatever reads bytes later. | 1 |
| **Canonical mode** | Line-buffered, editable input — backspace works, `read()` returns on Enter. | 2 |
| **`/dev/console`** | A real `vfs_node`, not a special-cased fd in the syscall dispatcher. | 2 |

---

## 🏗️ Chapter 1: The UART receive interrupt and its byte queue

### Where this actually stands

Confirmed by reading the tree, not assumed: **there is no RX code at all, on either
architecture.** `kernel/src/arch/x86_64/earlycon.c` only ever polls the line-status register and
transmits (`earlycon_putc`) — `earlycon_init()` explicitly writes `IER = 0x00`, disabling every
UART interrupt including receive. `kernel/src/arch/aarch64/earlycon.c` only does a single raw MMIO
write to the PL011 data register — no other register (flag, interrupt-mask, interrupt-clear) is
even defined yet.

Both architectures dispatch IRQs the same way today: a hardcoded `if`/`else` chain, not a table.

- **x86_64** — `x86_64_dispatch()` (`kernel/src/arch/x86_64/idt.c:154-176`) checks `irq_no == 0`
  for the timer and falls through to a `kprintf` no-op for everything else. **COM1 is IRQ 4**,
  already inside the 48 ISR stubs `isr.S` already builds (PIC vectors 0x20-0x2F, IRQ 0-15) — no
  new stub, no IDT change. Unmasking it is one already-exported call,
  `pic_set_irq_mask(4, true)` (`pic.h:30`), plus one new `else if (irq_no == 4)` branch.
- **AArch64** — `aarch64_dispatch()` (`kernel/src/arch/aarch64/exceptions.c:65-85`) checks
  `intid == GIC_INTID_VIRT_TIMER` and falls through the same way. `gic_init()`
  (`kernel/src/arch/aarch64/gic.c:20-39`) only enables `GICD_ISENABLER0` — **interrupt IDs 0-31
  only**. A PL011 SPI ID on QEMU `virt` is an SPI (≥ 32) — genuinely new territory. `gic.h` has no
  `GICD_ISENABLERn` for `n > 0` yet; enabling it needs the real formula (register `n = intid / 32`,
  bit `intid % 32`), not just flipping a bit in the register the timer already uses.

### The byte queue

This is PrOS's first real producer/consumer relationship outside the scheduler itself — the ISR
(producer) and whatever eventually drains it (consumer, Step 2's line discipline) run at different
times, with no lock needed as long as the producer side stays confined to interrupt context (IRQs
don't preempt each other on a single core the way tasks do). A fixed-size ring buffer with a
not-empty/not-full check is enough; what happens when it's full is a real, small decision — drop
the newest byte and let it be lost is the simplest policy, and matches what the UART's own hardware
FIFO already does before software ever sees a byte.

### Decisions worth naming before writing anything

| Decision | Options | Leaning |
|---|---|---|
| Queue full policy | drop newest vs drop oldest vs block the ISR | Drop newest — an ISR must never block, and typed input losing its most recent keystroke on a already-overflowing queue is the least surprising failure |
| TX interrupt-driven too? | yes vs stay polling | Stay polling — output already works via `earlycon_putc`, nothing in this Step needs it to change, and it's real scope this Step doesn't require |
| Queue size | small vs generous | A human types far slower than a byte queue drains between timer ticks; a few dozen bytes is almost certainly enough, and oversizing it just hides a real overflow policy bug instead of exercising it |

---

## 🏗️ Chapter 2: TTY line discipline and a real `/dev/console`

### Where this actually stands

`console_putc()` (`kernel/src/core/console.c:19-26`) fans out unconditionally to
`earlycon_putc()` then `fb_terminal_print_char()` — no buffering, no fd awareness, no notion of
"this byte belongs to task X's stdout." That's the *output* side, already working; this Step is
mostly about input, plus giving both directions a real file behind them instead of a syscall-level
special case.

The special case, exactly as it stands today (`kernel/src/syscall/syscall.c:8-16`):

```c
// special case for now
// no copy_from_user / copy_to_user for console_putc,
// that will be refactored into a proper /dev/console someday
static int64_t sys_write_console_or_vfs(int fd, const void *buf, uint64_t count) {
    if (fd == 1 || fd == 2) { /* loop console_putc, unvalidated */ }
    return sys_write(fd, buf, count);
}
```

That comment is this Step's actual acceptance criterion — retire the shim.

**The VFS already anticipates this.** `struct vfs_ops` (`kernel/include/fs/vfs/vfs.h:18-33`)
supports a fully custom node — `open`/`close`/`read`/`write`/`finddir`/`readdir`/`create` function
pointers, a `priv_data` slot for driver state, no coupling to `ramfs`'s storage at all. And
**`VFS_CHARDEVICE` (`0x04`) already exists as a node flag** (`vfs.h:12`), unused anywhere in the
tree — nothing built it yet, but the shape was clearly anticipated.

**Mounting needs no `/dev` directory tree.** `vfs_get_mountpoint()` (`kernel/src/fs/vfs/vfs.c:39-71`)
does longest-prefix matching over a flat mount list; `vfs_mount("/dev/console", console_node)`
directly — same call `ramfs` itself uses to mount `/` — makes `vfs_lookup("/dev/console")` resolve
straight to `console_node`, the matched prefix consuming the entire path before the lookup loop
ever tokenizes anything. A real `/dev` directory, with `finddir` support for enumerating it, is
more correct but is exactly the kind of scope this Step doesn't need yet — one worth revisiting
once a second device node ever exists.

### Canonical mode, the only mode this Step needs

**Canonical** (line-buffered, editable) is what `psh` needs — type, backspace to fix a typo,
Enter delivers the whole line. **Raw** (byte-by-byte, no editing, program sees every key
immediately) is a real POSIX TTY mode too, but nothing in this phase's payoff needs it — it's a
Phase 10 (PTY) concern, worth naming as explicitly out of scope rather than silently absent.

The state machine itself: accumulate bytes into a line buffer, echo each one back out through
`console_putc()` as it arrives (so the human sees what they typed), backspace (`0x08` or `0x7f`,
worth checking what the actual terminal sends) removes the last buffered byte *and* emits a
destructive-backspace echo sequence (`\b \b` — back up, overwrite with a space, back up again) so
the character visually disappears, Enter (`\r` or `\n`) delivers the completed line and a fresh
`read()` call gets it.

### Decisions worth naming before writing anything

| Decision | Options | Leaning |
|---|---|---|
| fd 0/1/2 at task creation | pre-open onto `/dev/console` for real vs keep a lighter fd-number special case | Pre-open for real — that's what retiring the shim actually means, and it's what every later program will expect to already be true |
| `struct file`'s `offset` for a character device | ignore it vs repurpose vs error if seeked | Ignore — POSIX character devices conventionally don't support seeking; `sys_lseek` on `/dev/console` returning an error is more honest than a number that means nothing |
| Where does the line buffer live | one global buffer vs per-open `struct file` | Per-open, via `priv_data` — a global buffer works with exactly one reader, which happens to be true today, but ties the design to that being permanently true |

---

## 🗺️ Chapter 3: The Steps

- **⬜ Step 1 — UART receive interrupt + byte queue.** Chapter 1 above. No user-visible behavior
  of its own — verified by a kernel-internal debug loop that drains the queue and echoes it back
  through the existing `console_putc()`, proving the interrupt fires and bytes flow, before
  anything downstream exists to consume them for real. Individually designed in
  [`PHASE6_STEP1_UART_INPUT.md`](PHASE6_STEP1_UART_INPUT.md).
- **⬜ Step 2 — TTY line discipline + `/dev/console`.** Chapter 2 above. A test program can
  `read()` a real, edited line typed at the keyboard from a real VFS node, and
  `sys_write_console_or_vfs`'s special case is gone. Individually designed in
  [`PHASE6_STEP2_TTY_CONSOLE.md`](PHASE6_STEP2_TTY_CONSOLE.md).
- **Step 3 — `getdents64` + `psh`.** Not designed here on purpose. Depends on Step 2's
  `/dev/console` being real, and may reshape once Steps 1-2 are actually built rather than just
  planned.

---

## 🕳️ Chapter 4: Traps worth knowing about in advance

- **The AArch64 GIC's `GICD_ISENABLERn` indexing isn't optional to get right.** The timer's
  interrupt ID happens to live in register 0 (`ISENABLER0`, IDs 0-31); a PL011 SPI on `virt`
  does not. Register index is `intid / 32`, bit is `intid % 32` — copying the timer's
  `gic_write(GICD_ISENABLER0, ...)` call verbatim with a different `intid` silently enables the
  wrong bit in the wrong register, or (if the offset math is missed entirely) the timer's own
  register, not a crash, just an interrupt that never fires and looks like dead hardware.
- **`earlycon_init()` deliberately disables every UART interrupt today (`IER = 0x00`).** Step 1
  needs to flip exactly the receive-data-available bit, not clear the whole register — writing a
  fresh `IER` value that only sets that one bit, rather than OR-ing it into whatever's already
  there, is the difference between "RX works" and "RX works but silently re-disables something
  Step 1 didn't mean to touch" if `IER` ever gains a second meaningful bit later.
- **A queue that's simple to write is easy to get wrong at the empty/full boundary.** The classic
  ring-buffer off-by-one — distinguishing "empty" from "full" when the write and read indices
  collide — is worth deciding explicitly (a count field alongside head/tail, or one wasted slot)
  rather than discovering it via a queue that silently reports "full" when it's actually empty.
- **A half-retired shim is worse than the shim.** `sys_write_console_or_vfs`'s special case and a
  real `/dev/console` node must not both partially work at once — callers assuming fd 1 always
  means "the special case" while others assume it means "a real fd" is the same "half-converted
  space" trap `-errno`'s C0 conversion warned about in Phase 5, one layer up.

---

## 📁 Critical files

Existing, to be modified:

- ⬜ `kernel/src/arch/x86_64/idt.c` — new `irq_no == 4` branch in `x86_64_dispatch()`
- ⬜ `kernel/src/arch/x86_64/earlycon.c` + `kernel/include/core/earlycon.h` — enable the UART RX
  interrupt, read the received byte off the data register
- ⬜ `kernel/src/arch/aarch64/gic.c` + `kernel/include/arch/aarch64/gic.h` — `GICD_ISENABLERn`
  for `n > 0`, a new `intid` branch in `aarch64_dispatch()`
- ⬜ `kernel/src/arch/aarch64/earlycon.c` + `kernel/include/core/earlycon.h` — real PL011 register
  offsets (flag, interrupt-mask-set/clear, interrupt-clear), not just the raw data register
- ⬜ `kernel/src/syscall/syscall.c` — `sys_write_console_or_vfs()` retired once `/dev/console` is
  real
- ⬜ `kernel/src/fs/vfs/vfs.c` — confirm/exercise `vfs_mount()` on a non-ramfs, custom-`vfs_ops`
  node for the first time

New:

- ⬜ A byte-queue implementation (ring buffer), architecture-neutral, fed by both ISRs
- ⬜ A TTY line-discipline implementation and the `/dev/console` `vfs_node` + `vfs_ops` backing it

---

## 🪜 Verification

The ring buffer's own logic (empty/full boundary, wraparound, drop-when-full policy) and the line
discipline's editing state machine (backspace, line completion) are both pure-data testable —
feed bytes in, check what comes out — independent of real hardware or booting, same spirit as
Phase 5's `copy_from_user` bounds-check tests. Only the actual interrupt wiring — does a real
keystroke over `-serial stdio` actually reach the queue — needs a real boot to prove.
