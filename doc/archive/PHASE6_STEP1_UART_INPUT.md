# Working Document: Phase 6 Step 1 — The UART Receive Interrupt and Its Byte Queue [STATUS: COMPLETE ✅]

> [!NOTE]
> **Phase 6 Step 1**, from [`PHASE6_TALK_BACK.md`](PHASE6_TALK_BACK.md) Chapter 1, expanded into
> Parts. Read that chapter for the *why*. This document is the *how*, against the code as it
> stands after Phase 5 — no RX code exists on either architecture; only polling transmit does.

> [!NOTE]
> Mentor-mode reminder (see the note at the top of [`../README.md`](../../README.md)): this is a
> design reference to code from by hand, not code to paste in. Signatures are given where the
> shape is settled; several decisions below are genuinely open and want your judgment call before
> anything is written.

---

## 🎯 What "done" looks like

> Typing a character over `-serial stdio` fires a real interrupt, on both architectures, and the
> byte lands in a queue. Nothing reads that queue for a human-facing reason yet — that's Step 2.

**What is explicitly *not* in this step:**

- ❌ **No line editing, no echo, no `/dev/console`.** A byte reaching the queue is the whole
  contract. What happens to it afterward is Step 2.
- ❌ **No TX interrupt.** Output stays exactly as polling as it already is — nothing here needs
  it to change, and making it interrupt-driven too is real, separate scope.
- ❌ **No IIR-based interrupt-cause disambiguation on x86_64.** Only the receive-data-available
  interrupt gets enabled, so any interrupt arriving on IRQ 4 unambiguously means "a byte is here"
  — worth revisiting only if a later step ever enables a second UART interrupt source.

---

## 🧠 The mental model, before any code

```
   UART hardware                    IRQ / IDT-or-GIC                 ring buffer
  ┌────────────────┐   byte      ┌────────────────────┐   push    ┌──────────────┐
  │ 16550 (x86_64)  │ ─────────▶ │ x86_64_dispatch()   │ ────────▶ │ head/tail,   │
  │ PL011 (AArch64) │  arrives   │ aarch64_dispatch()  │           │ fixed size,  │
  └────────────────┘             └────────────────────┘           │ drop-newest  │
                                    (already exists, this Step     │ on overflow  │
                                     adds one branch each)         └──────────────┘
                                                                          │
                                                                    Step 2 drains it
```

The queue is the one genuinely shared, architecture-neutral piece. Everything upstream of it —
enabling the interrupt, reading the byte off the hardware — is real per-architecture register
work with no shortcuts.

---

## ⚖️ Decisions to make before writing anything

### 1. The ring buffer's overflow policy

Chapter 1 already leans toward **drop the newest byte** when the queue is full — an ISR must
never block, and a human typing faster than the queue drains (which the queue's own size should
make vanishingly unlikely) losing its most recent keystroke is the least surprising failure.
Worth deciding the empty/full boundary explicitly too: a separate `count` field alongside
`head`/`tail` avoids the classic ring-buffer ambiguity where `head == tail` could mean either
"empty" or "full."

### 2. AArch64's PL011 interrupt ID — verify, don't assume

QEMU's `virt` machine commonly documents the PL011's interrupt as **SPI 1**, i.e. **GIC INTID
33** (`32 + 1`) — but this project has already been burned once by trusting a commonly-cited
architectural detail over what the actual hardware in front of it reports (Phase 4 Step 1's
AArch64 `EC` correction, found via QEMU's own `-d int` trace, not a manual). Confirm this the
same way before wiring `GICD_ISENABLER1`: boot with `-d int` and watch which `intid` actually
fires when a character arrives, or cross-check against the `virt` machine's generated DTB. Don't
hardcode `33` on faith.

### 3. Where the byte queue itself lives

Architecture-neutral code, `C` track — both `x86_64_dispatch()` and `aarch64_dispatch()` push
into the *same instance* of the ring buffer, the same way both architectures already feed the
same `timer_tick()`. One instance, not two, is the point: Step 2's consumer shouldn't need to
know which architecture is running underneath it. `C0` is now a reusable *type*, so this instance
needs a home too.

**Resolved:** `kernel/src/core/console_input.c` + `console_input.h`, same shape as `core/timer.c`
— the `static struct ring_buffer` and its backing array stay private to the file, never `extern`;
`console_input_init/push/pop()` are the only door in, mirroring `timer_tick()`/`timer_get_ticks()`.
`serial` produces bytes (raw chip access), `console_input` holds them, `console` will eventually
fan them out for real (Step 2) — three layers, one job each.

---

## 🗺️ Suggested order

```
  C0 ── the ring buffer itself, architecture-neutral, no hardware involved
  A1 ── x86_64: enable COM1's RX interrupt, unmask IRQ 4, read the byte, push to the queue
  B1 ── AArch64: real PL011 register offsets, enable RX interrupt, GICD_ISENABLER1, push to the queue
  C1 ── prove both: a debug echo loop that drains the queue back out through console_putc()
```

`C0` first — it's pure data structure, testable without booting, and both `A1`/`B1` need it to
exist before they have anywhere to push a byte. `A1`/`B1` have no dependency on each other; do
whichever architecture's register work you'd rather tackle first. `C1` is the merge point — the
first moment either architecture's real hardware exercises `C0` for real.

---

## 🧩 C0 — The ring buffer ✅ DONE (2026-08-17)

**Goal:** an instantiable, architecture-neutral byte queue type — `push(byte)` called from
interrupt context, `pop() -> optional byte` called from wherever Step 2 eventually drains it. Not
a singleton: a `struct` the caller owns, so the UART path isn't the only thing that ever gets to
have a ring buffer.

```c
// include/core/ring_buffer.h
// single-producer, single-consumer, drop-newest on overflow (decision 1)
struct ring_buffer {
    uint8_t *buf;    // caller-owned storage, we never alloc or free it
    size_t capacity; // size of buf, fixed for the instance's life
    size_t head;     // next slot to pop
    size_t tail;     // next slot to push
    size_t count;    // sidesteps the head==tail empty/full ambiguity
};

// zeroes head/tail/count, doesn't touch buf's contents
void ring_buffer_init(struct ring_buffer *rb, uint8_t *buf, size_t capacity);

// false and byte dropped if full
bool ring_buffer_push(struct ring_buffer *rb, uint8_t byte);

// false if empty, *out left untouched
bool ring_buffer_pop(struct ring_buffer *rb, uint8_t *out);
```

Storage is caller-owned on purpose — no heap involved, so a `static uint8_t` array plus a
`static struct ring_buffer` next to it is enough for the UART instance, and the ISR's `push` call
costs nothing extra over the old global version, just one more argument.

No locking needed as long as pushes only ever happen from interrupt context on one core — IRQs
don't preempt each other the way tasks do, so there's no concurrent-push case to guard against
yet. Worth a comment saying so explicitly, since it's an invariant a future SMP phase would break
silently rather than loudly.

**Verify:** pure-data — push more bytes than the buffer holds, confirm the oldest surviving byte
behaves per decision 1; pop from empty returns `false` rather than garbage; two independently
`init`'d instances don't share state. Landed as `kernel/src/core/test/test_ring_buffer.c`,
registered in `test.h`/`test.c` alongside `test_pmm`/`test_kstack`/etc — 11 assertions, including
10 rounds of push-3/pop-3 to cycle `head`/`tail` past the array boundary repeatedly, not just fill
it once. Passes on both architectures without booting into hardware.

---

# 🅰️ Track A — x86_64: COM1 receive interrupt

## A1 — Enable the interrupt, read the byte, feed the queue ✅ DONE (2026-08-17)

**Goal:** a real keystroke over `-serial stdio` produces a byte in `C0`'s queue.

Three pieces, all in `kernel/src/arch/x86_64/serial.c` / `idt.c`:

- **`serial_init()`'s `IER` write changes.** Today: `outb(COM1 + 1, 0x00)` — every UART
  interrupt disabled. Needs exactly the receive-data-available bit set (bit 0 of `IER`), and
  nothing else — write a fresh value with only that bit, don't accidentally leave a future second
  bit cleared by OR-ing carelessly.
- **Unmask IRQ 4 at the PIC.** `pic_set_irq_mask(4, true)` (`pic.h:30`) — already exported,
  already handles the cascade-line auto-unmask correctly.
- **`x86_64_dispatch()`'s branch** (`idt.c:154-176`) — a new `else if (irq_no == 4)` next to the
  existing `irq_no == 0` timer check. Read the received byte off the data register (`inb(COM1)`
  — same offset `serial_putc` transmits through, disambiguated by the UART's internal DLAB
  state, not by address), push it into `C0`'s queue, then the existing EOI logic at the bottom of
  `x86_64_dispatch()` already handles both PIC chips correctly regardless of which `irq_no`
  triggered it.

Landed slightly differently than sketched: `serial_init()` writes `IER` via read-modify-write
(`inb(COM1 + 1) | 0x01`) from the start rather than the plain literal write, skipping straight to
the "more honest pattern" the traps section below flags as only mattering later. `serial.h` also
gained `serial_getc(void)` (reads `inb(COM1)`) as its own named function rather than an inline
`inb(COM1)` call at the dispatch site — not originally scoped as a separate function, but keeps
`x86_64_dispatch()`'s new branch a one-liner: `console_input_push(serial_getc())`.

**Verify:** boot, type a character, confirm (via a temporary debug print inside the new branch,
removed once C1 lands) that the byte value matches what was typed. Confirmed via QEMU with
`-serial stdio` — typed characters echo on both the serial console and the framebuffer terminal.
The debug print actually landed one level deeper than sketched, inside `console_input_push()`
itself rather than at the `idt.c` call site — proves A1 (push happens, value is right) but means
it isn't the throwaway scaffolding C1 describes below; `console_input_pop()` still has zero
callers anywhere, so the read side stays unexercised outside of `C0`'s own test until C1's real
drain loop gets built.

---

# 🅱️ Track B — AArch64: PL011 receive interrupt

## B1 — Real PL011 registers, GIC enable, feed the queue ✅ DONE (2026-08-18)

**Goal:** the same outcome as A1, on AArch64.

`kernel/src/arch/aarch64/serial.c` today only defines the data register offset — everything
else needed here is genuinely new:

- **PL011 register offsets** (standard ARM PrimeCell layout, from `PL011_UART_BASE_PHYS`):
  `UARTDR` (data, `0x00`, already used), `UARTFR` (flags, `0x18` — bit 4 is "receive FIFO
  empty," worth checking before assuming a byte is ready), `UARTIMSC` (interrupt mask set/clear,
  `0x38` — bit 4 is the receive interrupt), `UARTICR` (interrupt clear, `0x44` — bit 4 clears a
  pending receive interrupt, must be written or the same interrupt fires forever).
- **Enable the receive interrupt**: set bit 4 of `UARTIMSC`.
- **`gic.h` needs `GICD_ISENABLERn` for `n > 0`.** `gic_init()` only ever touches
  `GICD_ISENABLER0` (IDs 0-31) — the PL011's SPI ID (decision 2 above) needs register `n = intid
  / 32`, bit `intid % 32`. Copying the timer's exact `gic_write(GICD_ISENABLER0, ...)` call with
  a different `intid` and no offset math silently enables the wrong bit in the wrong register.
- **`aarch64_dispatch()`'s branch** (`exceptions.c:65-85`) — a new `else if (intid == <the real
  PL011 intid>)` next to the existing `GIC_INTID_VIRT_TIMER` check. Read `UARTDR`, push to `C0`'s
  queue, write `UARTICR` to clear the pending interrupt (the existing `gic_eoi(intid)` call
  after the branch handles the GIC side; `UARTICR` is a separate, PL011-internal acknowledgment
  and both are needed).

Landed as designed, with decision 2 resolved by verification rather than faith: the PL011 is
**SPI 1, GIC INTID 33**, now named `GIC_INTID_PL011` in `gic.h:45`. `gic_enable_irq(uint32_t
intid)` was generalized rather than duplicated — one function computing `GICD_ISENABLER0 + (intid
/ 32) * 4` and bit `intid % 32`, so the timer's PPI 27 and the PL011's SPI 33 go through the same
call and `gic_init()` no longer writes `GICD_ISENABLER0` directly.

Two things landed beyond the sketch. `serial_rx_ready()` was added on both architectures (PL011
`UARTFR` bit 4, 16550 `LSR` bit 0) so the ISR **drains in a loop** rather than reading exactly
one byte — one interrupt can stand for several queued bytes, and reading only one leaves the rest
in the hardware FIFO. And `serial_init()` was split into `serial_init_put()` / `serial_init_get()`,
because transmit has to work from the first line of boot while receive can only be armed once the
interrupt controller is up — the same call could no longer serve both.

**Verify:** same as A1 — boot, type, confirm the byte value matches, via a temporary debug print.
Confirmed on AArch64 via QEMU `-serial stdio`.

---

## 🧩 C1 — Prove both: the echo loop ✅ DONE (2026-08-18)

**Goal:** a temporary debug loop — drain `C0`'s queue (polled from the boot loop, or on every
timer tick) and echo each byte straight back out through the existing `console_putc()`. Typing
`abc` over `-serial stdio` should print `abc` back. This is throwaway scaffolding, meant to be
deleted once Step 2's real consumer exists — its only job is proving the whole path (hardware →
interrupt → queue) works end to end, on both architectures, before Step 2 builds anything on top
of it.

Built exactly as throwaway scaffolding and deleted once Step 2's `/dev/console` became the real
consumer — which is what it was for. It did its job: it proved hardware → interrupt → queue on
both architectures before anything was built on top.

What it did **not** prove, and could not have, is the read side. The echo loop and the ISR both
lived in kernel context, so `console_input_pop()`'s only real caller arrived in Step 2 — see the
retrospective below for what that hid.

**Verify:** boot both architectures, type at each, confirm the echo. No unhandled-exception dump,
no dropped bytes under normal (human-speed) typing. Confirmed on both.

---

## 🕳️ Traps worth knowing about in advance

- **The AArch64 SPI ID is a guess until proven** — see decision 2. Get this wrong and the
  interrupt silently never fires; nothing crashes, nothing echoes, and there's no error message
  pointing at the cause.
- **`UARTICR` not being written leaves the interrupt permanently pending** on AArch64 — the GIC's
  own `gic_eoi()` isn't enough by itself, the PL011 has its own internal pending-interrupt state
  that only its own control register clears.
- **`IER`'s other bits matter on x86_64.** Writing a hardcoded `0x01` is fine today (nothing else
  uses `IER`), but OR-ing into whatever's currently there is the more honest pattern the moment
  anything else ever wants a second UART interrupt source.
- **The empty/full ring-buffer boundary** — see decision 1. Easy to write a queue that reports
  "full" when it's actually empty, or vice versa, if `head == tail` is used as the only signal.

---

## 📁 Files touched

New:

- ✅ `kernel/include/core/ring_buffer.h`, `kernel/src/core/ring_buffer.c` — the ring buffer type (C0)
- ✅ `kernel/src/core/console_input.c` + `kernel/include/core/console_input.h` — the concrete
  instance (decision 3), private `static struct ring_buffer`, `timer.c`-style wrapper functions
- ✅ `kernel/src/core/test/test_ring_buffer.c`, plus `test.h`/`test.c` registration — not
  originally scoped as its own file; `C0`'s "pure-data self-test" verify bullet, made concrete

Existing, to be modified:

- ✅ `kernel/src/arch/x86_64/serial.c` + `kernel/include/core/serial.h` — RX interrupt enabled,
  `serial_getc()` added (renamed from `earlycon` mid-Step, see decision 3 above)
- ✅ `kernel/src/arch/x86_64/idt.c` — new IRQ 4 branch in `x86_64_dispatch()`
- ✅ `kernel/src/arch/aarch64/serial.c` + `kernel/include/core/serial.h` — real PL011 register
  offsets (`UARTFR`/`UARTIMSC`/`UARTICR`), `serial_getc()`/`serial_rx_ready()` real on both arches,
  `serial_init()` split into `serial_init_put()`/`serial_init_get()`
- ✅ `kernel/src/arch/aarch64/gic.h` + `gic.c` — `gic_enable_irq(intid)` generalized to any
  `GICD_ISENABLERn`, `GIC_INTID_PL011` (33) named and verified
- ✅ `kernel/src/arch/aarch64/exceptions.c` — new `intid` branch in `aarch64_dispatch()`

---

## 🪜 Verification

- **C0** is pure data — push/pop/overflow behavior, no hardware or boot involved.
- **A1, B1, C1** are only observable by booting — a real interrupt firing from real (emulated)
  hardware, on real register state, is the actual thing being proven.

---

## 🔍 Retrospective — two things this Step got wrong, both found in Step 2

Written after the fact, because both cost real debugging time and neither was visible from inside
this Step.

**`console_input_init()` was never called.** It was written, declared, exported — and no caller
existed anywhere in the tree. `queue` stayed a zeroed static with `capacity == 0`, which makes
`ring_buffer_push()` take its `count == capacity` branch (`0 == 0`) and drop every byte, while
`ring_buffer_pop()` takes its `count == 0` branch and returns nothing. Both silent, no fault: the
capacity check short-circuits before anything dereferences the NULL `buf`.

C1's echo loop should have caught this and didn't — which is the lesson worth keeping. It echoed
from *inside* `console_input_push()` (see A1's note above) rather than by draining through
`console_input_pop()`, so it proved the ISR fired and the byte was right, and proved nothing about
the queue actually storing anything. **Scaffolding that taps the producer instead of exercising
the consumer isn't an end-to-end test**, it just looks like one. Fixed in Step 2 by calling
`console_input_init()` from `main()` before `serial_init_get()`.

**Decision 1's `count` field is a shared read-modify-write.** Choosing `count` over the
one-wasted-slot scheme did remove the `head == tail` ambiguity, and the "no locking needed as long
as pushes only ever happen from interrupt context" comment was right at the time — but only
because the *consumer* also happened to run with interrupts masked. Step 2 made syscalls
preemptible, which broke that unstated half of the invariant: `count++` in the ISR can now land
between the reader's load and store of `count--`, losing the increment. Fixed in Step 2 with a
real `spinlock_lock_irqsave()` around both sides. The comment was accurate and still misleading,
because it named one of the two conditions it depended on.
