#include "gic.h"

#include "core/kprintf.h"
#include "mm/pmm.h"

// These are device registers, not memory. Reads have side effects, GICC_IAR ack an interrupt just by being read,
// so the volatile here is to stop the compiler getting smart
static volatile uint32_t *gic_reg(uint64_t phys_base, uint64_t offset) {
    return (volatile uint32_t *)((uint8_t *)pmm_phys_to_virt(phys_base) + offset);
}

static uint32_t gic_read(uint64_t phys_base, uint64_t offset) {
    return *gic_reg(phys_base, offset);
}

static void gic_write(uint64_t phys_base, uint64_t offset, uint32_t value) {
    *gic_reg(phys_base, offset) = value;
}

void gic_init(void) {
    // First access here trigger a page fault on purpose:
    // the GIC sits below RAM, so it is inside the range try_handle_hhdm_mmio_fault() will handle.
    uint32_t typer = gic_read(GIC_DIST_PHYS_BASE, GICD_TYPER);
    // ITLinesNumber counts blocks of 32 lines, minus one
    uint32_t lines = ((typer & GICD_TYPER_IT_LINES_MASK) + 1) * GICD_TYPER_IT_LINES_UNIT;
    kprintf("GIC", "GICD_TYPER %#010x: %u interrupt lines\n", typer, lines);

    // The priority mask first, since leaving it at 0 makes everything below invisible:
    // nothing this CPU interface get would ever pass.
    gic_write(GIC_CPU_PHYS_BASE, GICC_PMR, GICC_PMR_ALLOW_ALL);

    // One bit per interrupt ID, write-1-to-set.
    // Zeros are ignored rather than clearing, so enabling one line needs no read-modify-write
    gic_write(GIC_DIST_PHYS_BASE, GICD_ISENABLER0, 1u << GIC_INTID_VIRT_TIMER);

    // The distributor forwards to the CPU interfaces, then this CPU's interface accepts.
    gic_write(GIC_DIST_PHYS_BASE, GICD_CTLR, GICD_CTLR_ENABLE);
    gic_write(GIC_CPU_PHYS_BASE, GICC_CTLR, GICC_CTLR_ENABLE);
}

uint32_t gic_acknowledge(void) {
    return gic_read(GIC_CPU_PHYS_BASE, GICC_IAR);
}

void gic_eoi(uint32_t intid) {
    gic_write(GIC_CPU_PHYS_BASE, GICC_EOIR, intid);
}
