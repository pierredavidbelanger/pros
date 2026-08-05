#include "exceptions.h"

#include "kprintf.h"
#include "arch.h"
#include "vmm.h"

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
    uint64_t vec_type = regs->vector_type;
    const char *vec_name = (vec_type < 16) ? vector_names[vec_type] : "Unknown Vector";

    uint32_t esr_ec = (regs->esr >> 26) & 0x3F;

    kprintf("\n================ [ AARCH64 EXCEPTION ] ================\n");
    kprintf(" Vector Slot %zu: %s\n", vec_type, vec_name);
    kprintf(" ESR_EL1: 0x%016lx (EC = 0x%02x)\n", regs->esr, esr_ec);
    kprintf(" FAR_EL1: 0x%016lx (Fault Address)\n", regs->far);
    kprintf(" ELR_EL1: 0x%016lx (Exception Link / PC)\n", regs->elr);
    kprintf(" SPSR_EL1: 0x%016lx  SP: 0x%016lx\n", regs->spsr, regs->sp);
    for (int i = 0; i < 30; i += 3) {
        kprintf(" X%02d: 0x%016lx  X%02d: 0x%016lx  X%02d: 0x%016lx\n",
                i, regs->x[i], i+1, regs->x[i+1], i+2, regs->x[i+2]);
    }
    kprintf(" X30 (LR): 0x%016lx\n", regs->x[30]);
    kprintf("=======================================================\n");

    if (esr_ec == 0x24 || esr_ec == 0x25) {
        // Data Abort
        uint64_t far = arch_vmm_get_fault_addr();
        if (vmm_handle_page_fault(far, regs->esr)) {
            // fault resolved, return to resume instruction
            return;
        }
    }

    kpanic("Unhandled AArch64 Exception");
}
