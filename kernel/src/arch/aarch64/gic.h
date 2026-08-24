#ifndef PROS_AARCH64_GIC_H
#define PROS_AARCH64_GIC_H

#include "stdc.h"

// GICv2 on the QEMU virt board, two separate MMIO blocks.
// The distributor is global: it holds the routing, the enable bits and the priorities of every line.
// The CPU interface is per core: it acknowledges, priority-masks and ends interrupts for one core.
// These addresses come from QEMU's hw/arm/virt.c memory map, they are not architectural.
// (that why i will comme up with an RSDP parser later, this smells)
#define GIC_DIST_PHYS_BASE 0x08000000ULL
#define GIC_CPU_PHYS_BASE 0x08010000ULL

// Distributor registers, offsets from GIC_DIST_PHYS_BASE
#define GICD_CTLR 0x000        // bit 0 enables forwarding, nothing reaches a CPU interface while it is clear
#define GICD_TYPER 0x004       // read only, describes what this distributor implements
#define GICD_ISENABLER0 0x100  // set-enable, one bit per line, this one covers lines 0-31

// CPU interface registers, offsets from GIC_CPU_PHYS_BASE
#define GICC_CTLR 0x000  // bit 0 enables this core's interface
#define GICC_PMR 0x004   // priority mask, only lines numerically below this pass. Resets to 0, which blocks everything
#define GICC_IAR 0x00C   // reading acknowledges the highest priority pending interrupt and returns its ID
#define GICC_EOIR 0x010  // writing an acknowledged ID back ends that interrupt

// GICD_TYPER low 5 bits are ITLinesNumber, the distributor supports 32 * (ITLinesNumber + 1) lines
#define GICD_TYPER_IT_LINES_MASK 0x1F
#define GICD_TYPER_IT_LINES_UNIT 32

// GICD_TYPER reads 0x00000008 here, so SecurityExtn (bit 10) is clear: no Security Extensions,
// which means bit 0 of both control registers is a plain Enable rather than EnableGrp0/EnableGrp1
#define GICD_CTLR_ENABLE (1u << 0)
#define GICC_CTLR_ENABLE (1u << 0)

// Priority runs backwards: an interrupt passes only if its priority is numerically *below* PMR,
// and PMR resets to 0, which blocks everything. 0xF0 rather than 0xFF because an implementation
// only has to implement the top 4 priority bits, the rest read as zero.
#define GICC_PMR_ALLOW_ALL 0xF0

// IDs 0-15 are SGIs (software generated, for inter-core signalling),
// 16-31 are PPIs (private peripheral, one set per core, for peripherals built into the CPU),
// 32 and up are SPIs (shared peripheral, out on the bus).
#define GIC_INTID_VIRT_TIMER 27  // EL1 virtual timer PPI, the EL1 physical timer is 30
#define GIC_INTID_SPURIOUS 1023  // GICC_IAR returns this when there was nothing to take, it must never be EOI'd

#define GIC_INTID_PL011 33  // SPI 1 (32 + 1)

void gic_init(void);

void gic_enable_irq(uint32_t intid);

// Acknowledge the pending interrupt and return its ID, which may be GIC_INTID_SPURIOUS.
uint32_t gic_acknowledge(void);

// End the interrupt, with the exact ID that gic_acknowledge() returned for it.
void gic_eoi(uint32_t intid);

#endif  // PROS_AARCH64_GIC_H
