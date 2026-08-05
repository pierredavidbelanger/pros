#include "idt.h"

#include "kprintf.h"
#include "arch.h"
#include "vmm.h"

static struct idt_entry idt_entries[256];
static struct idt_ptr   idt_pointer;

extern void *isr_stub_table[32];

static const char *exception_names[32] = {
    "Divide-by-zero (#DE)",
    "Debug (#DB)",
    "Non-maskable Interrupt (#NMI)",
    "Breakpoint (#BP)",
    "Overflow (#OF)",
    "Bound Range Exceeded (#BR)",
    "Invalid Opcode (#UD)",
    "Device Not Available (#NM)",
    "Double Fault (#DF)",
    "Coprocessor Segment Overrun",
    "Invalid TSS (#TS)",
    "Segment Not Present (#NP)",
    "Stack-Segment Fault (#SS)",
    "General Protection Fault (#GP)",
    "Page Fault (#PF)",
    "Reserved (15)",
    "x87 Floating-Point Exception (#MF)",
    "Alignment Check (#AC)",
    "Machine Check (#MC)",
    "SIMD Floating-Point Exception (#XM)",
    "Virtualization Exception (#VE)",
    "Control Protection Exception (#CP)",
    "Reserved (22)", "Reserved (23)", "Reserved (24)", "Reserved (25)",
    "Reserved (26)", "Reserved (27)", "Hypervisor Injection Exception (#HV)",
    "VMM Communication Exception (#VC)", "Security Exception (#SX)", "Reserved (31)"
};

void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt_entries[num].offset_low  = base & 0xFFFF;
    idt_entries[num].selector    = sel;
    idt_entries[num].ist         = 0;
    idt_entries[num].attributes  = flags;
    idt_entries[num].offset_mid  = (base >> 16) & 0xFFFF;
    idt_entries[num].offset_high = (base >> 32) & 0xFFFFFFFF;
    idt_entries[num].zero        = 0;
}

void idt_init(void) {
    idt_pointer.limit = (sizeof(struct idt_entry) * 256) - 1;
    idt_pointer.base  = (uint64_t)&idt_entries;

    // Populate CPU exception handlers 0..31
    for (uint8_t i = 0; i < 32; i++) {
        idt_set_gate(i, (uint64_t)isr_stub_table[i], 0x08, 0x8E); // 0x8E: Present, Ring 0, Interrupt Gate
    }

    asm volatile ("lidt %0" :: "m"(idt_pointer));
}

void x86_64_exception_handler(struct x86_64_registers *regs) {
    // Try to handle recoverable faults FIRST
    if (regs->int_no == 14) {
        // Page Fault
        uint64_t cr2 = arch_vmm_get_fault_addr();
        if (vmm_handle_page_fault(cr2, regs->error_code)) {
            // fault resolved, return to resume instruction!
            return;
        }
    }
    // Unrecoverable — dump everything for debugging
    kprintf("============= [ UNHANDLED EXCEPTION ] =============\n");
    const char *name = (regs->int_no < 32) ? exception_names[regs->int_no] : "Unknown Trap";
    kprintf(" Exception %zu: %s\n", regs->int_no, name);
    kprintf(" Error Code: 0x%lx\n", regs->error_code);
    kprintf(" RIP: 0x%016lx   CS: 0x%04lx   RFLAGS: 0x%016lx\n", regs->rip, regs->cs, regs->rflags);
    kprintf(" RSP: 0x%016lx   SS: 0x%04lx\n", regs->rsp, regs->ss);
    kprintf(" RAX: 0x%016lx  RBX: 0x%016lx  RCX: 0x%016lx\n", regs->rax, regs->rbx, regs->rcx);
    kprintf(" RDX: 0x%016lx  RSI: 0x%016lx  RDI: 0x%016lx\n", regs->rdx, regs->rsi, regs->rdi);
    kprintf(" RBP: 0x%016lx   R8: 0x%016lx   R9: 0x%016lx\n", regs->rbp, regs->r8, regs->r9);
    kprintf(" R10: 0x%016lx  R11: 0x%016lx  R12: 0x%016lx\n", regs->r10, regs->r11, regs->r12);
    kprintf(" R13: 0x%016lx  R14: 0x%016lx  R15: 0x%016lx\n", regs->r13, regs->r14, regs->r15);
    if (regs->int_no == 14) {
        // Page fault
        uint64_t cr2;
        asm volatile ("mov %%cr2, %0" : "=r"(cr2));
        kprintf(" CR2 (Faulting Linear Address): 0x%016lx\n", cr2);
    }
    kprintf("===================================================\n");
    kpanic("");
}
