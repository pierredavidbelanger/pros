# Working Document: Phase 3 Step 1 — Timers & Interrupts (no scheduler yet) [STATUS: NOT STARTED ⬜]

> [!NOTE]
> This is the first bite of [`PHASE3_USERLAND.md`](PHASE3_USERLAND.md) — its Part 10, Step 1,
> expanded into individually verifiable baby steps. Nothing here involves tasks, scheduling,
> userland, or privilege levels. Read Part 4 of the phase doc for the *why* behind the
> architecture choices; this document is the *how*, in order.

> [!NOTE]
> Mentor-mode reminder (see the note at the top of [`../README.md`](../README.md)): this is a
> design reference to code from by hand, not code to paste in. Register names, offsets,
> magic values and ordering constraints are given — the code isn't. Where a value is stated,
> it's worth confirming against the spec rather than trusting this file.

---

## 🎯 What "done" looks like

One acceptance criterion, and it's deliberately unambitious:

> On both architectures, booting prints a line roughly once per second for five seconds, then
> the kernel reaches `arch_shutdown()` and QEMU exits on its own, exactly like today.

That's it. No task switching. If that works, the hardest arch-specific plumbing in Phase 3 is
behind you, and Steps 2 and 3 are almost entirely generic C.

**What is explicitly *not* in this step** — worth writing down, because this is the step where
scope creep is most tempting:

- ❌ No `struct task`, no scheduler, no run queue.
- ❌ No changes to the exception *stack* handling in `vectors.S` (that's Step 2 — and it's a
  rewrite, so don't start it while also debugging a GIC).
- ❌ No `tss.rsp0`, no user segments, no ring 3.
- ❌ No `sys_nanosleep` / `sys_clock_gettime` — build the tick counter they'll need, expose
  nothing.

---

## 🧠 The mental model, before any code

Every interrupt on every architecture is the same five-link chain. Naming the links is most of
the battle, because when it doesn't work, the question "which link is broken?" is the only
useful question.

```
  ┌────────┐   1    ┌──────────────┐   2    ┌─────┐   3    ┌──────────────┐
  │ Device │───────►│  Interrupt   │───────►│ CPU │───────►│ Vector table │
  │(timer) │ raises │  controller  │ routes │     │ looks  │  → your stub │
  └────────┘  line  │ (PIC / GIC)  │  to a  └─────┘   up   └──────┬───────┘
                    └──────────────┘ vector                       │ 4
                            ▲                                     ▼
                            │ 5. acknowledge (EOI)         your C handler
                            └──────────────────────────────────────┘
```

1. **The device raises a line.** A timer counts down and asserts an output when it hits zero.
2. **The interrupt controller decides.** It holds a mask (is this line enabled?), a priority,
   and a mapping from *line number* to *CPU vector*. This is the part that differs most
   between the two architectures — it's a separate chip on x86 (the 8259 PIC) and a separate
   MMIO block on ARM (the GIC).
3. **The CPU takes the interrupt**, but only if interrupts are unmasked (`IF` in `rflags` on
   x86_64, the `I` bit in `DAIF` on AArch64). This is a second, independent mask from the
   controller's — and forgetting either one produces the identical symptom of total silence.
4. **The vector table dispatches** to a stub, which saves registers and calls C. This part
   already exists in the tree for exceptions and is reused wholesale.
5. **You acknowledge the interrupt** so the controller will send another one. Miss this and
   you get *exactly one* interrupt, forever.

Two masks, one acknowledgement. Nearly every "no ticks" bug in this step is one of those three.

### The one conceptual difference from what's already in the tree

The kernel already handles **exceptions** — synchronous, caused by the instruction that was
just executed (a page fault, a divide by zero). An **interrupt** is asynchronous: nothing the
running code did caused it, and it can land between *any* two instructions.

That distinction is why interrupts are hard in ways exceptions aren't. The handler must leave
the interrupted code *exactly* as it found it — every register, every flag. The existing stubs
already do that, which is why this step gets to reuse them. But it's also why Phase 3's later
steps care so much about the red zone, about reentrancy, and about which stack you're on.

---

## 🗺️ Suggested order

Do **one architecture completely**, then the other. Interleaving them means debugging two
unfamiliar interrupt controllers at once with a shared, also-new C abstraction in the middle.

Recommended: **x86_64 first.** Not because it's more elegant — the PIC is a 1981 part and it
shows — but because the IDT infrastructure already exists, so the first two baby steps are
nearly free and prove out the dispatch path before any hardware is involved.

> One exception: **B1 and B2** (read the ARM timer frequency, poll the timer with no interrupt
> controller at all) need nothing from any other step and take about twenty minutes. They're a
> good warm-up on a day when the PIC is being annoying.

```
  C0  shared skeleton  ──►  A1 ─ A2 ─ A3 ─ A4   (x86_64)
                       └─►  B1 ─ B2 ─ B3 ─ B4   (aarch64)
                                              └─►  C1  wire up & verify both
```

---

## 🧩 C0 — The shared skeleton (do this first, it's 20 minutes)

**Goal:** decide the seam between generic and arch-specific code *before* writing either side,
so the second architecture doesn't force a redesign.

Add to `kernel/include/arch/arch.h`, next to the existing `arch_vmm_*` block:

```c
// ─── Timer & Interrupt Architecture Primitives ───────────────
void arch_timer_init(uint32_t hz);
void arch_irq_enable(void);
void arch_irq_disable(void);
```

And a generic counter in `kernel/core/` — a `timer.c` with a `volatile uint64_t` tick count, a
`timer_tick(void)` that both architectures' IRQ handlers call, and a `timer_get_ticks(void)`.

**Why the counter is generic and not per-arch:** it's the thing every later feature reads —
the scheduler's time slice, `sys_clock_gettime`, `sys_nanosleep`. Only the *hardware* is
architecture-specific; "how many times has it fired" is not. Keeping the policy out of `arch/`
is the same discipline `arch_vmm_*` already follows, and it's why `vmm.c` is one file instead
of two.

**Why `volatile`:** the counter is written by an interrupt handler and read by normal code. To
the compiler, nothing in a `while (timer_get_ticks() < n);` loop modifies it, so it is entitled
to hoist the read out of the loop and spin forever on a stale value. This is a real bug that
will bite in C1 and is genuinely hard to see when it does. (`volatile` is *not* enough for
multi-CPU correctness later, but it is exactly right for "an interrupt handler on this same
CPU changed it".)

**Note:** `kernel/Makefile:30` globs `find core mm drivers fs -name '*.c'`, so anything under
`core/` is picked up with no build change.

---

# 🅰️ Track A — x86_64: PIC + PIT

## A1 — Make vector 32 reachable, with no hardware at all

**Goal:** prove the IDT → stub → C handler path works for a vector above 31, by triggering it
in software.

What to do:

- `kernel/arch/x86_64/isr.S` — the existing `ISR_NOERRCODE` macro already does exactly what a
  hardware IRQ needs (push a dummy error code, push the vector number, jump to the common
  stub). Extend the list to cover 32-47, and extend `isr_stub_table` to match.
- `kernel/arch/x86_64/idt.c` — `extern void *isr_stub_table[32]` (line 42) and the population
  loop (lines 121-125) both hard-code 32. Both need to grow. A `#define IDT_STUB_COUNT` used
  in all three places is worth it, since the two numbers silently disagreeing produces a jump
  through a garbage pointer.
- Keep the same gate flags (`0x8E`) that the exception gates use, and `ist = 0`.
- From `_start`, temporarily: `asm volatile ("int $32");`

**Verify:** the existing unhandled-exception dump prints, reporting exception 32. That
"failure" is the success — it means the CPU found your gate, ran your stub, and reached C with
a correct frame.

**Why do it this way:** you have just tested links 3, 4 and 5 of the chain with links 1 and 2
absent entirely. When the timer doesn't fire in A4, you already *know* the vector path is fine
and the bug is in the PIC or the PIT. Debugging is mostly the art of arranging to have already
eliminated things, and this costs almost nothing.

> Gate flags worth understanding rather than copying: `0x8E` = present, DPL 0, type 0xE
> (64-bit *interrupt* gate). The alternative, type 0xF, is a *trap* gate. The only difference:
> an interrupt gate clears `IF` on entry, a trap gate doesn't. So with `0x8E` your handler
> cannot itself be interrupted — which is what you want for now, and something you'll
> deliberately revisit much later.

## A2 — Route IRQs away from the panic path

**Goal:** vector 32 returns cleanly instead of dumping registers and halting.

What to do, in `x86_64_exception_handler` (`idt.c:130`): branch early on
`regs->int_no >= 32 && regs->int_no < 48`, handle it, and return before the dump. Call
`timer_tick()` for vector 32; for now, anything else can just print and return.

Send the **EOI** here, at the end of the IRQ branch: write `0x20` to port `0x20`. If the IRQ
number is ≥ 8 (i.e. it came via the slave PIC), write `0x20` to port `0xA0` *as well*, and in
that order.

`arch.c` has a `static void arch_outw` (line 18) but no `outb`/`inb` — both are needed, and
they belong next to it.

**Verify:** `int $32` prints one line and boot continues to shutdown.

**Why EOI exists at all:** the PIC tracks which interrupt it is currently servicing and
refuses to deliver anything of equal or lower priority until told the previous one is done.
That's what stops a fast device from re-entering your handler infinitely. The cost is that
forgetting the write silently wedges the line. **Symptom to memorise: exactly one interrupt
ever arrives.** If you see one tick and then silence in A4, come straight back here.

Also note the software `int $32` in A1 does *not* set the PIC's in-service bit, so sending an
EOI for an interrupt the PIC never delivered is harmless — but it does mean A2's test can't
validate your EOI. That's only verifiable in A4.

## A3 — Remap the PIC

**Goal:** move the PIC's vectors off the CPU's exception range, then mask everything.

The 8259 pair defaults to delivering IRQ 0-7 at vectors **8-15**. Look at
`exception_names[]` (`idt.c:44-59`): vector 8 is `#DF`, 10 is `#TS`, 13 is `#GP`, 14 is `#PF`.
A timer tick would be indistinguishable from a double fault. **This is the entire reason
remapping exists**, and it's the single most-copied-without-understanding snippet in hobby OS
development.

Ports: master `0x20`/`0x21`, slave `0xA0`/`0xA1`. The even port is command, the odd is data.

The init sequence is four writes per chip, in a fixed order (the chip is a state machine — it
expects ICW2, ICW3, ICW4 immediately after ICW1, on the data port):

| Word | Master | Slave | Meaning |
|---|---|---|---|
| ICW1 | `0x11` → `0x20` | `0x11` → `0xA0` | begin init, expect ICW4 |
| ICW2 | `0x20` → `0x21` | `0x28` → `0xA1` | **vector base** — this is the remap |
| ICW3 | `0x04` → `0x21` | `0x02` → `0xA1` | master: slave is on IRQ2; slave: "I am 2" |
| ICW4 | `0x01` → `0x21` | `0x01` → `0xA1` | 8086 mode |

Then write the mask (OCW1) to the data ports: `0xFF` on both = everything masked.

**Verify:** nothing visible. Boot must still be clean. That's the whole check.

**Why mask everything now:** you have not written a handler for the keyboard, the RTC, or the
spurious IRQ 7 that the PIC generates on electrical noise. Unmasking selectively in A4 means
the first interrupt you ever receive is one you're expecting.

> If you find `outb(0x80, 0)` between PIC writes in reference code — that's an `io_wait`, a
> delay for genuinely ancient hardware that couldn't keep up with back-to-back port writes.
> QEMU doesn't need it. Include it or not, but knowing *why* it's there beats cargo-culting it.

## A4 — Program the PIT, unmask, and enable

**Goal:** actual ticks.

The 8254 PIT runs from a fixed 1,193,182 Hz oscillator and divides it by a 16-bit value you
supply. So `divisor = 1193182 / hz`, and for 100 Hz that's 11,932.

- Command port `0x43`, channel 0 data port `0x40`.
- Command byte `0x36`: channel 0, access mode lobyte-then-hibyte, **mode 3 (square wave)**,
  binary counting.
- Then write the divisor low byte then high byte, both to `0x40`.

Mode 3 matters: it **auto-reloads**. The counter reaches zero, fires, and reloads from the
divisor with no software involvement. Remember that when you get to B4, where the ARM timer
does the opposite.

Then: clear bit 0 of the master's mask to unmask IRQ 0, and `sti` to set `IF`.

`arch_timer_init(hz)` is the natural home for all of the above plus A3's remap.

**Verify:** the tick counter climbs. Print every 100th tick rather than every tick — at 100 Hz
an unconditional `kprintf` to a bitmap-font framebuffer will bury the boot log and may well be
slower than the tick period, which is its own confusing failure.

**Why 100 Hz:** fast enough that a scheduler time slice feels responsive, slow enough that the
handler's cost is irrelevant. Linux used 100 Hz for years. Anything from 50 to 1000 works;
pick one and put the reason in a comment.

**If nothing happens, in this order:** is `IF` actually set (did `sti` run, and does anything
before the tick do a `cli`)? Is the mask byte right (`0xFE`, not `0xFF`)? Did the divisor get
written as two bytes to `0x40`? Is the IDT gate at 32 populated? A1 already proved the last one
— which is exactly why A1 was worth doing.

---

# 🅱️ Track B — AArch64: Generic Timer + GICv2

The ARM side splits cleanly into "the timer" (in the CPU, easy, system registers) and "the
interrupt controller" (a separate MMIO block, fiddly). Doing them in that order means the
first half needs no interrupts at all.

## B1 — Read `CNTFRQ_EL0` and print it

**Goal:** confirm system-register access to the Generic Timer and learn its frequency.

`mrs x, cntfrq_el0` gives the timer's tick rate in Hz. On QEMU `virt` expect **62,500,000**
(62.5 MHz). Print it.

**Why bother with a whole step for one register read:** because the divisor arithmetic in B2
depends on it, and because unlike the PIT's fixed 1.19 MHz, this value is *not* architectural
— it's whatever the firmware programmed. Hard-coding it is a portability bug that would only
surface on real hardware, long after you'd stopped thinking about timers.

## B2 — Poll the timer, with no GIC and no interrupts

**Goal:** make the timer fire and observe it, using nothing but system registers.

Three registers, all `_EL0`-suffixed but accessible from EL1:

| Register | Purpose |
|---|---|
| `CNTV_TVAL_EL0` | write a countdown value; it decrements at `CNTFRQ_EL0` |
| `CNTV_CTL_EL0` | bit 0 `ENABLE`, bit 1 `IMASK` (1 = don't signal), bit 2 `ISTATUS` (read-only: fired) |
| `CNTVCT_EL0` | the free-running virtual counter, if you want elapsed time |

So: write `CNTFRQ_EL0 / hz` to `CNTV_TVAL_EL0`, set `CNTV_CTL_EL0` to `ENABLE | IMASK`
(enabled but not signalling anything to the GIC, which isn't configured yet), then spin reading
`CNTV_CTL_EL0` until `ISTATUS` is set. Print, and stop.

**Verify:** one "timer fired" line, roughly when you expected it.

**Why the *virtual* timer (CNTV) and not the physical one (CNTP):** both exist. The physical
timer's accessibility from EL1 depends on `CNTHCTL_EL2` bits that a hypervisor or the
bootloader controls, and Limine drops the kernel to EL1 through EL2. The virtual timer is
reliably usable from EL1 on QEMU `virt`. Worth confirming rather than trusting — but if CNTP
mysteriously traps, this is why, and it is *not* a bug in your code.

**Why poll first:** identical reasoning to A1. This proves link 1 of the chain in isolation. If
B4 produces no ticks, you already know the timer itself is fine and the GIC is at fault.

## B3 — Touch the GIC and read one register back

**Goal:** prove MMIO access to the GIC works before configuring anything through it.

On QEMU `virt` (verify against QEMU's `hw/arm/virt.c` memory map rather than trusting this):

- **GICD** (distributor, global): physical `0x0800_0000`
- **GICC** (CPU interface, per-core): physical `0x0801_0000`

Read `GICD_TYPER` (offset `0x004`) and print it. Its low 5 bits encode how many interrupt lines
the distributor supports, as `32 * (ITLinesNumber + 1)`.

Two things to get right:

- **Access it through the HHDM**, i.e. `pmm_phys_to_virt(0x08000000)`. And note something nice:
  you do **not** need to map it. `try_handle_hhdm_mmio_fault()` (`mm/vmm.c:206-219`)
  demand-maps any HHDM address below `pmm_get_max_phys_addr()` with `VMM_CACHE_DISABLE` on
  first touch — and `0x0800_0000` sits below RAM (which starts at `0x4000_0000` on `virt`), so
  it qualifies. Your first read takes a page fault, the existing Phase 1 handler resolves it,
  and the read completes. **This is Phase 1 paying for itself**; worth stepping through once to
  watch it happen.
- **Use `volatile uint32_t *` and 32-bit accesses.** These are device registers, not memory:
  reads have side effects (`GICC_IAR` *acknowledges* an interrupt just by being read), and the
  compiler must not merge, reorder, cache or widen them.

**Verify:** a plausible number (`GICD_TYPER & 0x1F` of 4 → 160 lines).

## B4 — Configure the GIC, dispatch the IRQ, re-arm

**Goal:** ticks.

This is the biggest single baby step in Step 1. Four independent things must all be right,
which is exactly why B1-B3 exist to have already eliminated the timer and the MMIO path.

**1. Enable the controller.** Minimum viable GICv2 init:

| Register | Offset | Value | Why |
|---|---|---|---|
| `GICD_CTLR` | GICD + `0x000` | 1 | enable the distributor (nothing is forwarded otherwise) |
| `GICD_ISENABLER0` | GICD + `0x100` | bit 27 | enable interrupt ID 27 |
| `GICC_PMR` | GICC + `0x004` | `0xF0` | priority mask: only interrupts *numerically below* this get through. Left at reset 0, **nothing ever passes** |
| `GICC_CTLR` | GICC + `0x000` | 1 | enable this CPU's interface |

**Why 27:** interrupt IDs 0-15 are SGIs (software-generated, for inter-CPU signalling), 16-31
are **PPIs** — private peripheral interrupts, one set per core, for things built into the CPU
like the timer. 32+ are SPIs, shared peripherals on the bus. The EL1 *virtual* timer is PPI
**27**; the EL1 physical timer is 30. Since B2 chose CNTV, the number is 27 — enable the wrong
one and everything else is perfect and nothing fires.

`GICC_PMR` deserves the callout: it is the single most common cause of a silent GICv2. Reset
value is 0, and 0 masks everything.

**2. Let the timer actually signal.** Clear `IMASK` (bit 1) in `CNTV_CTL_EL0`, which B2 set.

**3. Unmask at the CPU.** `msr daifclr, #2` clears the `I` bit. This is the ARM equivalent of
`sti`, and it is the second of the two independent masks from the mental model. Note
`arch_halt()` (`arch/aarch64/arch.c:35-38`) does `msr daifset, #2` before `wfi` — it
deliberately masks interrupts, so it can't be used as an idle loop that waits for ticks.

**4. Dispatch it.** An IRQ from EL1 with SP_EL1 enters the vector table at **`VECTOR_ENTRY 5`**
(`vectors.S:51`), which today falls straight into the unhandled-exception panic
(`exceptions.c:41-53`). In `aarch64_exception_handler`, branch on `regs->vector_type == 5`
before the panic path, and:

- read **`GICC_IAR`** (GICC + `0x00C`) to acknowledge — this both tells you *which* interrupt
  fired and moves it to "active" state,
- if the ID is **1023**, it's the spurious ID: return immediately, and do **not** EOI it,
- handle it (`timer_tick()`),
- **re-arm the timer** — write `CNTV_TVAL_EL0` again. The ARM timer is a **one-shot**: it fires
  and stays fired. Unlike the PIT's mode 3, nothing reloads it for you. **Symptom of
  forgetting: exactly one tick** — the same symptom as a missing EOI on x86, from a completely
  different cause.
- write the ID from `IAR` back to **`GICC_EOIR`** (GICC + `0x010`).

Write the value you got from `IAR`, not the constant 27. They're the same here, but EOI-ing an
ID you didn't acknowledge corrupts the controller's state in a way that's miserable to debug
the day a second interrupt source exists.

**Verify:** the tick counter climbs. Same advice as A4 — print every 100th.

**If nothing happens:** `GICC_PMR` still 0? `GICD_CTLR`/`GICC_CTLR` not enabled? `IMASK` still
set from B2? `daifclr` not run? Bit 27 vs 30? B1-B3 have already cleared the timer and the MMIO
path, so it is one of those five.

---

## 🧩 C1 — Wire it up and prove both

**Goal:** the acceptance criterion at the top of this document.

In `core/main.c`, after `arch_init()` and the memory subsystems (the handler calls `kprintf`,
so the console must exist first): call `arch_timer_init(100)`, then `arch_irq_enable()`, then
wait for ~5 seconds of ticks before falling through to `arch_shutdown()` (line 67).

For the wait, spin on `timer_get_ticks()`. **Do not** use `arch_halt()` — both implementations
mask interrupts before halting, so it would wait forever for a tick that can no longer arrive.
A plain busy loop on the `volatile` counter is correct and honest for now; a proper idle that
sleeps the CPU (`sti; hlt` / bare `wfi`) is worth adding when there's an actual idle task.

Worth considering while you're here: gate the per-second print behind the existing
`pros.tests`-style cmdline check (`main.c:31-33`), or drop it to one summary line, so a normal
boot doesn't grow five seconds of chatter permanently.

**Verify on both:** `make` runs both architectures back to back, and `logs/qemu-*.log` keeps
the output. Five lines each, then a clean shutdown.

---

## 🔬 Debugging tools worth knowing before you need them

This step has a failure mode the kernel hasn't had before: **total silence**, or a reset with
no output. The existing register dump can't help if the handler is never reached. QEMU can.

- **`-d int`** logs every exception and interrupt QEMU takes, including the vector number.
  This is the single most useful flag in this step — it tells you directly whether link 3
  (CPU takes it) is happening, which splits the search space in half.
- **`-d int,cpu_reset`** additionally logs resets, which is how you catch a triple fault. A
  triple fault reboots the machine silently; without this you're guessing.
- **`-d guest_errors`** reports writes to invalid device registers — useful for a mistyped GIC
  offset.
- **`-D logs/qemu-debug.log`** sends all of that to a file instead of interleaving with your
  serial output, which matters since the existing targets already pipe serial through
  `ansifilter | tee`.
- **`-monitor telnet:...`** or `Ctrl+A` then `C` gets you the QEMU monitor: `info registers`
  dumps CPU state, and on x86 `info pic` shows the PIC's mask and in-service registers — i.e.
  it answers "did I actually unmask IRQ 0?" without any guessing.

These are throwaway invocations, not Makefile targets — run `qemu-system-x86_64` by hand with
the flags while debugging. If one earns its keep repeatedly, *then* it's worth a target.

---

## 🕳️ Failure modes, collected

Grouped by symptom, since that's how you'll meet them.

**Exactly one interrupt, then silence.**
- x86: missing or misdirected EOI (and remember IRQ ≥ 8 needs both PICs).
- ARM: forgot to re-arm `CNTV_TVAL_EL0`. The timer is one-shot.
- Two different causes, one identical symptom, one per architecture. Almost poetic.

**Total silence.**
- One of the two masks: controller-level (`GICC_PMR`/`GICD_ISENABLER`, or the PIC's OCW1) or
  CPU-level (`IF` / `DAIF.I`).
- Wrong interrupt number (PPI 30 instead of 27; unmasked the wrong PIC bit).
- Vector never populated — which A1 exists to rule out in advance.

**Register dump reporting a weird exception number.**
- On x86, if you see `#DF` or `#GP` at boot where you expected a tick, the PIC remap didn't
  take. That *is* the timer, arriving at vector 8.

**Silent reboot loop, no output at all.**
- Triple fault. `-d int,cpu_reset`. Most likely a bad IDT gate — check that `isr_stub_table`
  and the population loop agree on their length.

**Ticks arrive but the kernel behaves strangely afterwards.**
- The handler isn't preserving something. Reuse the existing stubs unmodified; they already
  save and restore the full frame, and this is precisely the step where inventing a new,
  leaner save path is a false economy.

**The wait loop in C1 never finishes even though ticks are printing.**
- The tick counter isn't `volatile`, and the compiler hoisted the load out of the loop.

---

## 📁 Files touched

- ⬜ `kernel/include/arch/arch.h` — the three new primitives
- ⬜ `kernel/core/timer.c` (new) — `volatile` tick counter, `timer_tick()`, `timer_get_ticks()`
- ⬜ `kernel/include/core/timer.h` (new)
- ⬜ `kernel/arch/x86_64/isr.S` — stubs 32-47, longer `isr_stub_table`
- ⬜ `kernel/arch/x86_64/idt.c` — table length, population loop, IRQ branch + EOI in the handler
- ⬜ `kernel/arch/x86_64/pic.c` + `pit.c` (new, or one `timer.c`) — remap, divisor, masks,
  `outb`/`inb`
- ⬜ `kernel/arch/aarch64/gic.c` (new) — GICv2 init, `gic_acknowledge()`, `gic_eoi()`
- ⬜ `kernel/arch/aarch64/timer.c` (new) — `CNTFRQ`/`CNTV_TVAL`/`CNTV_CTL`, arm and re-arm
- ⬜ `kernel/arch/aarch64/exceptions.c` — IRQ branch on `vector_type == 5`
- ⬜ `kernel/core/main.c` — init, enable, wait, then the existing shutdown

Deliberately **not** touched: `vectors.S`'s stack handling, `tss_init()`, `fs/`, anything in
`mm/`. If a change wants to reach into those, it belongs to Step 2 or later.

## 🪜 Verification

`core/test/` can't assert on this — a tick count is a property of elapsed time, and a self-test
that sleeps is a self-test that hangs when the thing it's testing is broken. The check is the
boot log.

One thing *is* worth a real assertion later, once the counter exists and Step 3 needs it to be
trustworthy: that `timer_get_ticks()` is monotonic and that N ticks at H Hz took roughly N/H
seconds by an independent clock (`CNTVCT_EL0` on ARM, `rdtsc` on x86). That's a genuine test of
the divisor arithmetic, which is otherwise the easiest thing here to get quietly wrong — a
factor-of-ten error in the PIT divisor looks completely normal in a boot log.
