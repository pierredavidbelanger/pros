# Some side quest i want to do at some point

## CLion setup

i still cant autocomplete nor correctly CTRL-click navigate using my current CLion setup, since i dont use cmake.
I really need to fix that stuff

## ACPI table walker, to stop hardcoding device addresses

**Where this came from:** `arch/aarch64/gic.h` hardcodes `GIC_DIST_PHYS_BASE 0x08000000` and
`GIC_CPU_PHYS_BASE 0x08010000`, taken from QEMU's `hw/arm/virt.c`. Everything *else* in that
header (the register offsets, PPI 27) is architectural GICv2 and portable — only the two base
addresses are a board decision. Same class of assumption already exists in
`arch/aarch64/earlycon.c:7` (PL011 at `0x09000000`) and in `arch_shutdown()`, which hardcodes
`hvc` as the PSCI conduit when `hvc` vs `smc` is a firmware-described property.

Unlike x86 — where `0x20`/`0x40`/`0x43` are a 1981 IBM PC contract every vendor still honours —
AArch64 has no legacy map to fall back on. Where the GIC sits is up to whoever integrated the
SoC, so the address has to be *discovered*, not assumed.

**Why ACPI and not device tree.** The obvious answer is a DTB, and QEMU `virt` really does build
one — but it hands it to the firmware, not to us. We boot `ovmf-code-aarch64.fd` (edk2/AAVMF)
from pflash, which consumes QEMU's FDT and publishes ACPI to the OS instead. Limine sources its
DTB from a UEFI configuration table that AAVMF doesn't republish when ACPI is present, which is
why `dtb_request.response` is NULL while `rsdp_request.response` is populated — **on both
architectures**. So on this platform ACPI is the only oracle actually available.
(`-machine virt,acpi=off` flips the firmware to the DT path, if the other side is ever worth
seeing.)

It's also the better investment: an FDT parser would only ever serve aarch64, while an ACPI
walker gets reused on x86 for the IOAPIC (to retire the LAPIC virtual-wire hack in `lapic.c`),
the HPET, and SMP bringup later. And it's the easier parser — native endian, fixed-layout
structs, none of device tree's big-endian everything or `#address-cells` inheritance.

**Shape of it:** RSDP → XSDT → find the table with signature `"APIC"` (the MADT) → iterate its
subtables by `(type, length)`.

| Type | Structure | Gives |
|---|---|---|
| `0x0B` | GIC CPU Interface (GICC) | CPU interface base, one entry per core |
| `0x0C` | GIC Distributor (GICD) | distributor base, **and the GIC version** |
| `0x0E` | GIC Redistributor (GICR) | GICv3 only — its presence is itself the signal |

The version field in `0x0C` matters more than the addresses: on GICv3 the CPU interface is system
registers (`ICC_*`) and a CPU interface base is meaningless, so this is what lets `gic_probe()`
refuse a machine it doesn't understand instead of writing into whatever happens to be there.

**Traps, collected in advance:**

- RSDP revision 0 has a 32-bit `RsdtAddress`; revision ≥ 2 adds the 64-bit `XsdtAddress`. Prefer
  XSDT, don't assume it exists.
- Verify the checksums (all bytes summing to 0 mod 256; the RSDP has two, first 20 bytes then the
  whole thing). They turn a wrong pointer into an immediate error instead of confusing garbage.
- The structures are packed and genuinely unaligned, especially the MADT subtable stream. `memcpy`
  into a local rather than casting a pointer and dereferencing.
- Check whether Limine hands the RSDP as a physical or an HHDM address — `limine.h:482` types it
  `void *`, which settles nothing, and this has moved across base revisions (we're on 6). Print it
  next to `hhdm_request.response->offset` once and compare.
- Not a problem here, but the classic version of this bug: ACPI tables live in `ACPI_RECLAIMABLE`
  memory, and a PMM that pools it will allocate over them before they're parsed. `pmm.c:40` only
  accepts `LIMINE_MEMMAP_USABLE`, so ours survive.

**Do it after Phase 3 Step 1 is green on both architectures.** The current constants are *known
correct*, which is exactly what should be holding still while the GIC itself is being learned.
Then change how the addresses are obtained, keeping the hardcoded pair in `gic.h` as a documented
fallback for when there's no ACPI or no matching entry.

## Kill `ansifilter`: the boot log loses its tail exactly when it matters

**Where this came from:** all four QEMU targets in the root `Makefile` end with
`| ansifilter | tee logs/qemu-<arch>.log`. The log is complete when the kernel shuts itself down,
and truncated when the run is interrupted — the *hang* case, which is the one worth reading.
`logs/qemu-aarch64.log` currently stops dead right after `[IRQ  ] Enable IRQ`.

**Root cause.** `ansifilter` is C++ and writes through a `std::ostream`. When stdout is a pipe
instead of a TTY that buffer is ~4 KB and only drains when full or at EOF. Ctrl-C in a terminal
raises `SIGINT` on the entire foreground *process group* — QEMU, `ansifilter` and `tee` all at once
— so `ansifilter` dies with a full buffer, and the default `SIGINT` disposition doesn't run static
destructors or flush anything. Up to 4 KB of the most interesting output evaporates.
`stdbuf -o0` does **not** help (measured): it `LD_PRELOAD`s a shim that retunes *stdio*'s buffer,
and an iostream that isn't synced through stdio never consults it. This is the general shape of the
bug — any filter sitting in a pipeline that buffers is a place where output can die.

**Option A — take the filter out of the log path entirely.** QEMU can write the log itself:
`-chardev stdio,id=ser0,signal=on,logfile=logs/qemu-aarch64.log -serial chardev:ser0` replacing
`-serial stdio` (`logfile` / `logappend` / `signal` all confirmed present on qemu 10.2.2 here).
QEMU writes bytes to that fd as the guest emits them, so even a half-printed line before a hang is
on disk. Cost: the log is byte-exact, escape codes and all — which is the thing `ansifilter` was
added to hide.

**Option B — write our own filter, in `tools/`.** `tools/` is empty and this is a genuinely small,
self-contained exercise: `read()` into a buffer, run a state machine, `write()` immediately, never
hold anything back. The grammar is ECMA-48 and smaller than it looks — a CSI sequence is `ESC [`,
then parameter bytes `0x30–0x3F`, then intermediate bytes `0x20–0x2F`, then exactly one final byte
`0x40–0x7E` that ends it; OSC is `ESC ]` up to `BEL` or `ESC \`; and a handful of two-byte escapes
(`ESC (B`, `ESC =`, `ESC 7`) round it out. ~50 lines of C, no dependency, nothing hidden, and it can
never lose a byte because it never owns one for longer than a syscall. A host tool built by the root
`Makefile`, not kernel code.

**Option C — `sed -u`.** Works (measured: streams per line, survives `SIGINT`), takes five minutes,
and reads like line noise in a Makefile. Recorded as the stopgap, not the destination.

**Leaning:** A + B. QEMU's `logfile=` for raw ground truth that can't be lost, our own filter for
the readable/greppable one. B alone is enough for the reported symptom.

**Traps, collected in advance:**

- Any *line*-oriented filter still holds a trailing partial line (no `\n`) until EOF. If the kernel
  dies mid-`kprintf`, that fragment only survives if the filter writes per `read()` rather than per
  line — or if Option A is also in place.
- Keep `signal=on` explicit on the `stdio` chardev if Ctrl-C should go on killing QEMU;
  `signal=off` hands Ctrl-C to the guest instead.
- Verify empirically that QEMU's `logfile=` really is an unbuffered `write(2)` and not something
  with a buffer of its own — the whole point is to not repeat this bug one layer down.
- Unrelated but adjacent: `make` sees the exit status of the *last* command in a pipeline, so a
  happy `tee` masks a failing QEMU. `.SHELLFLAGS` with `-o pipefail` fixes that if it ever bites.
- `tee` itself is fine — it writes what it reads, nothing to flush.
- `logs/` is already wiped by `make clean`, so extra log files there need no new bookkeeping.
- `README.md` documents `ansifilter` as a host dependency (in the requirements list, and a paragraph
  explaining the pipeline under "Headless / No-Window Runs") — both need updating when this lands.

**When:** any time — independent of the GIC work, and doing it *before* the next long aarch64
hang-debugging session pays for itself immediately.
