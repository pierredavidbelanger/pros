#include "exceptions.h"

#include "gic.h"
#include "timer.h"
#include "core/serial.h"
#include "core/kprintf.h"
#include "core/timer.h"
#include "core/console_input.h"
#include "arch/arch.h"
#include "mm/vmm.h"
#include "proc/sched.h"
#include "proc/task.h"
#include "syscall/syscall.h"

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

// Dump and die. Never returns.
static void panic_unhandled(struct trap_frame *frame, const char *what) {
    uint32_t esr_ec = (frame->esr >> 26) & 0x3F;
    kprintf("ARM64", "============= [ %s ] =============\n", what);
    const char *vec_name = (frame->vector_type < 16) ? vector_names[frame->vector_type] : "Unknown";
    kprintf("ARM64", " Vector %zu: %s\n", frame->vector_type, vec_name);
    kprintf("ARM64", " ESR_EL1: 0x%016lx (EC=0x%02x)\n", frame->esr, esr_ec);
    kprintf("ARM64", " FAR_EL1: 0x%016lx\n", frame->far);
    kprintf("ARM64", " ELR_EL1: 0x%016lx\n", frame->elr);
    kprintf("ARM64", " SPSR_EL1: 0x%016lx  SP: 0x%016lx\n", frame->spsr, frame->sp);
    for (int i = 0; i < 30; i += 3) {
        kprintf("ARM64", " X%02d: 0x%016lx  X%02d: 0x%016lx  X%02d: 0x%016lx\n", i, frame->x[i], i+1, frame->x[i+1], i+2, frame->x[i+2]);
    }
    kprintf("ARM64", " X30 (LR): 0x%016lx\n", frame->x[30]);
    kprintf("ARM64", "==================== [ TASKS ] ====================\n");
    task_dump_all();
    kprintf("ARM64", "===================================================\n");
    kpanic("");
}

static void aarch64_dispatch(struct trap_frame *frame) {
    // Slots 0-3 are Current EL on SP_EL0 (EL1t)
    // unreachable since arch_init() selected SP_EL1,
    // 12-15 are AArch32, which this kernel never enters.
    // Arriving in either means the vector table or SPSel is not what this code believes
    if (frame->vector_type < 4 || frame->vector_type > 11) {
        panic_unhandled(frame, "UNEXPECTED VECTOR SLOT");
    }

    // IRQ vector slots:
    // 5 = Current EL SP_EL1 IRQ
    // 9 = Lower EL AArch64 IRQ
    if (frame->vector_type == 5 || frame->vector_type == 9) {
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
        } else if (intid == GIC_INTID_PL011) {
            // drain fully as one interrupt can represent more than one queued byte
            while (serial_rx_ready()) {
                console_input_push((uint8_t) serial_getc());
            }
        } else {
            kprintf("ARM64", "HANDLED IRQ %u: nothing\n", intid);
        }

        // EOI, needs the be the one we ack, otherwise we will have a bad time
        gic_eoi(intid);
        return;
    }

    uint32_t esr_ec = (frame->esr >> 26) & 0x3F;

    // kprintf("ARM64", "frame->vector_type %zu esr_ec %u\n", frame->vector_type, esr_ec);

    // Handle Lower EL AArch64 Synchronous SVC
    if (frame->vector_type == 8 && esr_ec == 0x15) {
        // kprintf("ARM64", "SVC: syscall_dispatch %zu ...\n", frame->x[8]);
        int64_t ret = syscall_dispatch(frame->x[8], frame->x[0], frame->x[1], frame->x[2], frame->x[3], frame->x[4], frame->x[5]);
        // kprintf("ARM64", "SVC: syscall_dispatch %zu DONE ret %lld\n", frame->x[8], ret);
        frame->x[0] = (uint64_t) ret;
        // Resolved: CPU will eret back and continue (syscall return is in x0)
        return;
    }

    // Handle Page Fault : vmm_handle_page_fault
    // EC=0x20: Instruction Abort from lower EL
    // EC=0x21: Instruction Abort from current EL
    // EC=0x24: Data Abort from lower EL
    // EC=0x25: Data Abort from current EL
    if (esr_ec == 0x20 || esr_ec == 0x21 || esr_ec == 0x24 || esr_ec == 0x25) {
        if (vmm_handle_page_fault(frame->far, frame->esr)) {
            // Resolved: CPU will eret back and re-execute
            return;
        }
    }

    // Unrecoverable — dump register state
    panic_unhandled(frame, "UNHANDLED EXCEPTION");
}

struct trap_frame *aarch64_exception_handler(struct trap_frame *frame) {
    aarch64_dispatch(frame);
    return sched_on_trap_exit(frame);
}
