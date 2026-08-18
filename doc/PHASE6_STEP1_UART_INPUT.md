# Working Document: Phase 6 Step 1 — The UART Receive Interrupt and Its Byte Queue [STATUS: NOT STARTED ⬜]

> [!NOTE]
> **Phase 6 Step 1**, from [`PHASE6_TALK_BACK.md`](PHASE6_TALK_BACK.md) Chapter 1, expanded into
> Parts. Read that chapter for the *why*. This document is the *how*, against the code as it
> stands after Phase 5 — no RX code exists on either architecture; only polling transmit does.

> [!NOTE]
> Mentor-mode reminder (see the note at the top of [`../README.md`](../README.md)): this is a
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
into the *same* queue implementation, the same way both architectures already feed the same
`timer_tick()`. One queue, not two, is the point: Step 2's consumer shouldn't need to know which
architecture is running underneath it.

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

## 🧩 C0 — The ring buffer

**Goal:** a fixed-size, architecture-neutral byte queue — `push(byte)` called from interrupt
context, `pop() -> optional byte` called from wherever Step 2 eventually drains it.

```c
// include/core/input_queue.h
// single-producer (either arch's ISR), single-consumer, drop-newest on overflow
void input_queue_push(uint8_t byte);
bool input_queue_pop(uint8_t *out);
```

No locking needed as long as pushes only ever happen from interrupt context on one core — IRQs
don't preempt each other the way tasks do, so there's no concurrent-push case to guard against
yet. Worth a comment saying so explicitly, since it's an invariant a future SMP phase would break
silently rather than loudly.

**Verify:** pure-data — push more bytes than the buffer holds, confirm the oldest surviving byte
and the drop count behave per decision 1; pop from empty returns nothing rather than garbage.

---

# 🅰️ Track A — x86_64: COM1 receive interrupt

## A1 — Enable the interrupt, read the byte, feed the queue

**Goal:** a real keystroke over `-serial stdio` produces a byte in `C0`'s queue.

Three pieces, all in `kernel/src/arch/x86_64/earlycon.c` / `idt.c`:

- **`earlycon_init()`'s `IER` write changes.** Today: `outb(COM1 + 1, 0x00)` — every UART
  interrupt disabled. Needs exactly the receive-data-available bit set (bit 0 of `IER`), and
  nothing else — write a fresh value with only that bit, don't accidentally leave a future second
  bit cleared by OR-ing carelessly.
- **Unmask IRQ 4 at the PIC.** `pic_set_irq_mask(4, true)` (`pic.h:30`) — already exported,
  already handles the cascade-line auto-unmask correctly.
- **`x86_64_dispatch()`'s branch** (`idt.c:154-176`) — a new `else if (irq_no == 4)` next to the
  existing `irq_no == 0` timer check. Read the received byte off the data register (`inb(COM1)`
  — same offset `earlycon_putc` transmits through, disambiguated by the UART's internal DLAB
  state, not by address), push it into `C0`'s queue, then the existing EOI logic at the bottom of
  `x86_64_dispatch()` already handles both PIC chips correctly regardless of which `irq_no`
  triggered it.

**Verify:** boot, type a character, confirm (via a temporary debug print inside the new branch,
removed once C1 lands) that the byte value matches what was typed.

---

# 🅱️ Track B — AArch64: PL011 receive interrupt

## B1 — Real PL011 registers, GIC enable, feed the queue

**Goal:** the same outcome as A1, on AArch64.

`kernel/src/arch/aarch64/earlycon.c` today only defines the data register offset — everything
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

**Verify:** same as A1 — boot, type, confirm the byte value matches, via a temporary debug print.

---

## 🧩 C1 — Prove both: the echo loop

**Goal:** a temporary debug loop — drain `C0`'s queue (polled from the boot loop, or on every
timer tick) and echo each byte straight back out through the existing `console_putc()`. Typing
`abc` over `-serial stdio` should print `abc` back. This is throwaway scaffolding, meant to be
deleted once Step 2's real consumer exists — its only job is proving the whole path (hardware →
interrupt → queue) works end to end, on both architectures, before Step 2 builds anything on top
of it.

**Verify:** boot both architectures, type at each, confirm the echo. No unhandled-exception dump,
no dropped bytes under normal (human-speed) typing.

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

- ⬜ `kernel/include/core/input_queue.h`, `kernel/src/core/input_queue.c` — the ring buffer (C0)

Existing, to be modified:

- ⬜ `kernel/src/arch/x86_64/earlycon.c` + `kernel/include/core/earlycon.h` — enable RX interrupt
- ⬜ `kernel/src/arch/x86_64/idt.c` — new IRQ 4 branch in `x86_64_dispatch()`
- ⬜ `kernel/src/arch/aarch64/earlycon.c` + `kernel/include/core/earlycon.h` — real PL011 register
  offsets beyond the data register
- ⬜ `kernel/src/arch/aarch64/gic.h` + `gic.c` — `GICD_ISENABLERn` for `n > 0`
- ⬜ `kernel/src/arch/aarch64/exceptions.c` — new `intid` branch in `aarch64_dispatch()`

---

## 🪜 Verification

- **C0** is pure data — push/pop/overflow behavior, no hardware or boot involved.
- **A1, B1, C1** are only observable by booting — a real interrupt firing from real (emulated)
  hardware, on real register state, is the actual thing being proven.
