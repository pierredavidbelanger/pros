#include "exceptions.h"

#include "gic.h"
#include "timer.h"
#include "core/earlycon.h"
#include "core/kprintf.h"
#include "core/timer.h"
#include "arch/arch.h"
#include "mm/vmm.h"

static const char *vector_names[16] = {
    "Current EL SP_EL0 Synchronous",
    "Current EL SP_EL0 IRQ",
    "Current EL SP_EL0 FIQ",
    "Current EL SP_EL0 SError",
    "Current EL SP_EL1 Synchronous",
    "Current EL SP_EL1 IRQ",
    "Current EL SP_EL1 FIQ",
    "Current EL SP_EL1 SError",
    "Lower EL AArch64 Synchronous",
    "Lower EL AArch64 IRQ",
    "Lower EL AArch64 FIQ",
    "Lower EL AArch64 SError",
    "Lower EL AArch32 Synchronous",
    "Lower EL AArch32 IRQ",
    "Lower EL AArch32 FIQ",
    "Lower EL AArch32 SError"
};

void aarch64_exception_handler(struct aarch64_registers *regs) {
    // IRQ vector slots:
    // 1 = current EL on SP_EL0,
    // 5 = current EL on SP_EL1
    // (and 9 = lower EL, once userland exists).
    // Limine leaves SPSel = 0, so kernel IRQs arrive as 1 today.
    if (regs->vector_type == 1 || regs->vector_type == 5) {
        uint32_t intid = gic_acknowledge();

        // Nothing left to take.
        // Return without an EOI: ending an interrupt that was never acknowledged corrupts the controller state
        if (intid == GIC_INTID_SPURIOUS) {
            return;
        }

        if (intid == GIC_INTID_VIRT_TIMER) {
            // Rearm before the EOI.
            arm_timer_rearm();
            timer_tick();
        } else {
            kprintf("ARM64", "HANDLED IRQ %u: nothing\n", intid);
        }

        // EOI, needs the be the one we ack, otherwise we will have a bad time
        gic_eoi(intid);
        return;
    }

    uint32_t esr_ec = (regs->esr >> 26) & 0x3F;

    // EC=0x20: Instruction Abort from lower EL
    // EC=0x21: Instruction Abort from current EL
    // EC=0x24: Data Abort from lower EL
    // EC=0x25: Data Abort from current EL
    if (esr_ec == 0x20 || esr_ec == 0x21 || esr_ec == 0x24 || esr_ec == 0x25) {
        if (vmm_handle_page_fault(regs->far, regs->esr)) {
            return;  // Resolved — CPU will eret back and re-execute
        }
    }

    // Unrecoverable — dump register state
    kprintf("ARM64", "============= [ UNHANDLED EXCEPTION ] =============\n");
    const char *vec_name = (regs->vector_type < 16) ? vector_names[regs->vector_type] : "Unknown";
    kprintf("ARM64", " Vector %zu: %s\n", regs->vector_type, vec_name);
    kprintf("ARM64", " ESR_EL1: 0x%016lx (EC=0x%02x)\n", regs->esr, esr_ec);
    kprintf("ARM64", " FAR_EL1: 0x%016lx\n", regs->far);
    kprintf("ARM64", " ELR_EL1: 0x%016lx\n", regs->elr);
    kprintf("ARM64", " SPSR_EL1: 0x%016lx  SP: 0x%016lx\n", regs->spsr, regs->sp);
    for (int i = 0; i < 30; i += 3) {
        kprintf("ARM64", " X%02d: 0x%016lx  X%02d: 0x%016lx  X%02d: 0x%016lx\n", i, regs->x[i], i+1, regs->x[i+1], i+2, regs->x[i+2]);
    }
    kprintf("ARM64", " X30 (LR): 0x%016lx\n", regs->x[30]);
    kprintf("ARM64", "===================================================\n");
    kpanic("");
}
