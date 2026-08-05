#include "exceptions.h"
#include "core/earlycon.h"

#include "core/kprintf.h"
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
    uint32_t esr_ec = (regs->esr >> 26) & 0x3F;

    // EC=0x20: Instruction Abort from lower EL
    // EC=0x21: Instruction Abort from current EL
    // EC=0x24: Data Abort from lower EL
    // EC=0x25: Data Abort from current EL
    if (esr_ec == 0x20 || esr_ec == 0x21 || esr_ec == 0x24 || esr_ec == 0x25) {
        uint64_t far = regs->far;
        if (vmm_handle_page_fault(far, regs->esr)) {
            return;  // Resolved — CPU will eret back and re-execute
        }
    }

    // Unrecoverable — dump register state
    kprintf("============= [ UNHANDLED EXCEPTION ] =============\n");
    const char *vec_name = (regs->vector_type < 16)
                           ? vector_names[regs->vector_type] : "Unknown";
    kprintf(" Vector %zu: %s\n", regs->vector_type, vec_name);
    kprintf(" ESR_EL1: 0x%016lx (EC=0x%02x)\n", regs->esr, esr_ec);
    kprintf(" FAR_EL1: 0x%016lx\n", regs->far);
    kprintf(" ELR_EL1: 0x%016lx\n", regs->elr);
    kprintf(" SPSR_EL1: 0x%016lx  SP: 0x%016lx\n", regs->spsr, regs->sp);
    for (int i = 0; i < 30; i += 3) {
        kprintf(" X%02d: 0x%016lx  X%02d: 0x%016lx  X%02d: 0x%016lx\n",
                i, regs->x[i], i+1, regs->x[i+1], i+2, regs->x[i+2]);
    }
    kprintf(" X30 (LR): 0x%016lx\n", regs->x[30]);
    kprintf("===================================================\n");
    kpanic("");
}
