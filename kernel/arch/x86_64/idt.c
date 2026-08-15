#include "idt.h"

#include "io.h"
#include "pic.h"
#include "core/kprintf.h"
#include "arch/arch.h"
#include "mm/vmm.h"
#include "core/memory.h"
#include "core/timer.h"
#include "proc/sched.h"
#include "proc/task.h"

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
    struct gdt_entry user_reserved;  // 0x28, sysret's 32-bit compat CS slot, never loaded in 64-bit mode
    struct gdt_entry user_data;      // 0x30 -> SS = 0x33
    struct gdt_entry user_code;      // 0x38 -> CS = 0x3b
} __attribute__((packed, aligned(16))) gdt;

static struct gdt_ptr gdt_pointer;
static struct tss64 sys_tss __attribute__((aligned(16)));
static uint8_t x86_64_exception_stack[16384] __attribute__((aligned(16)));

static struct idt_entry idt_entries[256];
static struct idt_ptr   idt_pointer;

#define IDT_STUB_COUNT 48 // must match the stubs and table in isr.S
_Static_assert(IDT_STUB_COUNT >= PIC_VECTOR_BASE + PIC_IRQ_COUNT, "isr.S has no stubs for the PIC's vector range");

#define IDT_EXCEPTION_COUNT 32

#define IDT_INT_IS_IRQ(int_no) ((int_no) >= PIC_VECTOR_BASE && (int_no) < PIC_VECTOR_BASE + PIC_IRQ_COUNT)
#define IDT_INT_TO_IRQ(int_no) ((int_no) - PIC_VECTOR_BASE)

extern void *isr_stub_table[IDT_STUB_COUNT];

static const char *exception_names[IDT_EXCEPTION_COUNT] = {
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
    gdt.null_entry =    (struct gdt_entry){0, 0, 0, 0, 0, 0};
    gdt.kernel_code =   (struct gdt_entry){0xFFFF, 0, 0, 0x9A, 0xAF, 0}; // 0x08
    gdt.kernel_data =   (struct gdt_entry){0xFFFF, 0, 0, 0x92, 0xCF, 0}; // 0x10
    gdt.user_reserved = (struct gdt_entry){0, 0, 0, 0, 0, 0};
    gdt.user_data =     (struct gdt_entry){0xFFFF, 0, 0, 0xF2, 0xCF, 0}; // 0x92 | 0x60 (DPL 3)
    gdt.user_code =     (struct gdt_entry){0xFFFF, 0, 0, 0xFA, 0xAF, 0}; // 0x9A | 0x60 (DPL 3)

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

    // Populate CPU interrupt handlers up to (excluding) IDT_STUB_COUNT
    for (uint8_t i = 0; i < IDT_STUB_COUNT; i++) {
        // Use IST1 (ist = 1) for Double Fault (#DF = 8)
        uint8_t ist = i == 8 ? 1 : 0;
        idt_set_gate(i, (uint64_t)isr_stub_table[i], 0x08, 0x8E, ist);
    }

    asm volatile ("lidt %0" :: "m"(idt_pointer));
}

void idt_tss_set_rsp0(void *rsp0) {
    sys_tss.rsp0 = (uint64_t) rsp0;
}

static void x86_64_dispatch(struct trap_frame *frame) {
    uint64_t cr2;
    asm volatile ("mov %%cr2, %0" : "=r"(cr2));

    if (frame->int_no == 14) {
        // Page Fault — try demand paging
        if (vmm_handle_page_fault(cr2, frame->error_code)) {
            return;  // Resolved — CPU will re-execute the faulting instruction
        }
    }

    if (IDT_INT_IS_IRQ(frame->int_no)) {
        uint64_t irq_no = IDT_INT_TO_IRQ(frame->int_no);
        if (irq_no == 0) {
            timer_tick();
        } else {
            kprintf("X8664", "HANDLED %zu: nothing\n", frame->int_no);
        }
        // EOI - End Of Interrupt
        if (irq_no >= PIC_IRQS_PER_CHIP) outb(PIC_SLAVE_CMD, PIC_EOI);  // slave, only when it came via the cascade
        outb(PIC_MASTER_CMD, PIC_EOI);                                  // master, always
        return;
    }

    // Unrecoverable — dump register state
    kprintf("X8664", "============= [ UNHANDLED EXCEPTION ] =============\n");
    const char *name = (frame->int_no < IDT_EXCEPTION_COUNT) ? exception_names[frame->int_no] : "Unknown";
    kprintf("X8664", " Exception %zu: %s\n", frame->int_no, name);
    kprintf("X8664", " Error Code: 0x%lx\n", frame->error_code);
    kprintf("X8664", " RIP: 0x%016lx  CS: 0x%04lx  RFLAGS: 0x%016lx\n", frame->rip, frame->cs, frame->rflags);
    kprintf("X8664", " RSP: 0x%016lx  SS: 0x%04lx\n", frame->rsp, frame->ss);
    kprintf("X8664", " RAX: 0x%016lx  RBX: 0x%016lx  RCX: 0x%016lx\n", frame->rax, frame->rbx, frame->rcx);
    kprintf("X8664", " RDX: 0x%016lx  RSI: 0x%016lx  RDI: 0x%016lx\n", frame->rdx, frame->rsi, frame->rdi);
    kprintf("X8664", " RBP: 0x%016lx   R8: 0x%016lx   R9: 0x%016lx\n", frame->rbp, frame->r8, frame->r9);
    kprintf("X8664", " R10: 0x%016lx  R11: 0x%016lx  R12: 0x%016lx\n", frame->r10, frame->r11, frame->r12);
    kprintf("X8664", " R13: 0x%016lx  R14: 0x%016lx  R15: 0x%016lx\n", frame->r13, frame->r14, frame->r15);
    if (frame->int_no == 14) {
        kprintf("X8664", " CR2: 0x%016lx\n", cr2);
    }
    kprintf("X8664", "==================== [ TASKS ] ====================\n");
    task_dump_all();
    kprintf("X8664", "===================================================\n");
    kpanic("");
}

struct trap_frame *x86_64_exception_handler(struct trap_frame *frame) {
    x86_64_dispatch(frame);
    return sched_on_trap_exit(frame);
}
