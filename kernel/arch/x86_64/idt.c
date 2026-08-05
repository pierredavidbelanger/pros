#include "idt.h"

#include "core/kprintf.h"
#include "arch/arch.h"
#include "mm/vmm.h"
#include "core/memory.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct tss_gdt_entry {
    struct gdt_entry low;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct {
    struct gdt_entry null_entry;
    struct gdt_entry kernel_code;
    struct gdt_entry kernel_data;
    struct tss_gdt_entry tss_entry;
} __attribute__((packed, aligned(16))) gdt;

static struct gdt_ptr gdt_pointer;
static struct tss64 sys_tss __attribute__((aligned(16)));
static uint8_t x86_64_exception_stack[16384] __attribute__((aligned(16)));

static struct idt_entry idt_entries[256];
static struct idt_ptr   idt_pointer;

extern void *isr_stub_table[32];

static const char *exception_names[32] = {
    "Divide-by-zero (#DE)", "Debug (#DB)",
    "Non-maskable Interrupt (#NMI)", "Breakpoint (#BP)",
    "Overflow (#OF)", "Bound Range Exceeded (#BR)",
    "Invalid Opcode (#UD)", "Device Not Available (#NM)",
    "Double Fault (#DF)", "Coprocessor Segment Overrun",
    "Invalid TSS (#TS)", "Segment Not Present (#NP)",
    "Stack-Segment Fault (#SS)", "General Protection Fault (#GP)",
    "Page Fault (#PF)", "Reserved (15)",
    "x87 FP Exception (#MF)", "Alignment Check (#AC)",
    "Machine Check (#MC)", "SIMD FP Exception (#XM)",
    "Virtualization Exception (#VE)", "Control Protection (#CP)",
    "Reserved (22)", "Reserved (23)", "Reserved (24)", "Reserved (25)",
    "Reserved (26)", "Reserved (27)", "Hypervisor Injection (#HV)",
    "VMM Communication (#VC)", "Security Exception (#SX)", "Reserved (31)"
};

static void tss_init(void) {
    gdt.null_entry  = (struct gdt_entry){0, 0, 0, 0, 0, 0};
    gdt.kernel_code = (struct gdt_entry){0xFFFF, 0, 0, 0x9A, 0xAF, 0}; // 0x08
    gdt.kernel_data = (struct gdt_entry){0xFFFF, 0, 0, 0x92, 0xCF, 0}; // 0x10

    memset(&sys_tss, 0, sizeof(sys_tss));
    uint64_t ist1_top = (uint64_t)&x86_64_exception_stack[16384];
    sys_tss.ist[0] = ist1_top; // IST1 = Index 0 in ist array

    uint64_t tss_base = (uint64_t)&sys_tss;
    uint32_t tss_limit = sizeof(struct tss64) - 1;

    gdt.tss_entry.low.limit_low   = tss_limit & 0xFFFF;
    gdt.tss_entry.low.base_low    = tss_base & 0xFFFF;
    gdt.tss_entry.low.base_middle = (tss_base >> 16) & 0xFF;
    gdt.tss_entry.low.access      = 0x89; // Present, Ring 0, 64-bit TSS Available
    gdt.tss_entry.low.granularity = (tss_limit >> 16) & 0x0F;
    gdt.tss_entry.low.base_high   = (tss_base >> 24) & 0xFF;
    gdt.tss_entry.base_upper      = (uint32_t)(tss_base >> 32);
    gdt.tss_entry.reserved        = 0;

    gdt_pointer.limit = sizeof(gdt) - 1;
    gdt_pointer.base  = (uint64_t)&gdt;

    asm volatile (
        "lgdt %0\n\t"
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%ss\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov $0x18, %%ax\n\t"
        "ltr %%ax\n\t"
        :: "m"(gdt_pointer) : "rax", "memory"
    );
}

static void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags, uint8_t ist) {
    idt_entries[num].offset_low  = base & 0xFFFF;
    idt_entries[num].selector    = sel;
    idt_entries[num].ist         = ist & 0x07;
    idt_entries[num].attributes  = flags;
    idt_entries[num].offset_mid  = (base >> 16) & 0xFFFF;
    idt_entries[num].offset_high = (base >> 32) & 0xFFFFFFFF;
    idt_entries[num].zero        = 0;
}

void idt_init(void) {
    tss_init();

    idt_pointer.limit = (sizeof(struct idt_entry) * 256) - 1;
    idt_pointer.base  = (uint64_t)&idt_entries;

    // Populate CPU exception handlers 0..31
    for (uint8_t i = 0; i < 32; i++) {
        // Use IST1 (ist = 1) for Double Fault (#DF = 8) and Page Fault (#PF = 14)
        uint8_t ist = (i == 8 || i == 14) ? 1 : 0;
        idt_set_gate(i, (uint64_t)isr_stub_table[i], 0x08, 0x8E, ist);
    }

    asm volatile ("lidt %0" :: "m"(idt_pointer));
}

void x86_64_exception_handler(struct x86_64_registers *regs) {
    if (regs->int_no == 14) {
        // Page Fault — try demand paging
        uint64_t cr2 = arch_vmm_get_fault_addr();
        if (vmm_handle_page_fault(cr2, regs->error_code)) {
            return;  // Resolved — CPU will re-execute the faulting instruction
        }
    }

    // Unrecoverable — dump register state
    kprintf("============= [ UNHANDLED EXCEPTION ] =============\n");
    const char *name = (regs->int_no < 32) ? exception_names[regs->int_no] : "Unknown";
    kprintf(" Exception %zu: %s\n", regs->int_no, name);
    kprintf(" Error Code: 0x%lx\n", regs->error_code);
    kprintf(" RIP: 0x%016lx  CS: 0x%04lx  RFLAGS: 0x%016lx\n",
            regs->rip, regs->cs, regs->rflags);
    kprintf(" RSP: 0x%016lx  SS: 0x%04lx\n", regs->rsp, regs->ss);
    kprintf(" RAX: 0x%016lx  RBX: 0x%016lx  RCX: 0x%016lx\n",
            regs->rax, regs->rbx, regs->rcx);
    kprintf(" RDX: 0x%016lx  RSI: 0x%016lx  RDI: 0x%016lx\n",
            regs->rdx, regs->rsi, regs->rdi);
    kprintf(" RBP: 0x%016lx   R8: 0x%016lx   R9: 0x%016lx\n",
            regs->rbp, regs->r8, regs->r9);
    kprintf(" R10: 0x%016lx  R11: 0x%016lx  R12: 0x%016lx\n",
            regs->r10, regs->r11, regs->r12);
    kprintf(" R13: 0x%016lx  R14: 0x%016lx  R15: 0x%016lx\n",
            regs->r13, regs->r14, regs->r15);
    if (regs->int_no == 14) {
        uint64_t cr2;
        asm volatile ("mov %%cr2, %0" : "=r"(cr2));
        kprintf(" CR2: 0x%016lx\n", cr2);
    }
    kprintf("===================================================\n");
    kpanic("");
}
