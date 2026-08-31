#include "arch/arch.h"

#include "idt.h"
#include "switch_frame.h"
#include "pic.h"
#include "lapic.h"
#include "percpu.h"
#include "io.h"
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "core/memory.h"
#include "core/kprintf.h"
#include "proc/task.h"
#include "proc/elf.h"

void syscall_init(void);

void arch_init(void) {
    idt_init();
    syscall_init();
    pic_init();
}

void arch_cpu_relax(void) {
    asm volatile("pause");
}

void arch_idle(void) {
    // sti and hlt must stay adjacent,
    // the shadow (sti inhibit for one instruction) is what makes this race-free
    asm volatile("sti; hlt; cli" ::: "memory");
}

void arch_halt(void) {
    for (;;) {
        asm volatile ("cli; hlt");
    }
}

void arch_shutdown(void) {
    // QEMU q35 fallback port
    outw(0x604, 0x2000);
    // Fallback: QEMU debug-exit port
    outw(0x501, 0x00);
    // Fallback: halt if poweroff is not supported by hardware
    arch_halt();
}

// ─── Syscall: syscall/sysret MSRs ────────────────────────────

#define IA32_EFER_SCE (1ULL << 0)

// A3: arch/x86_64/syscall_entry.S
extern void syscall_entry(void);

// the one static per-CPU instance, for now
struct x86_64_percpu x86_64_percpu0;

void syscall_init(void) {
    // preserve LME/NXE etc, only SCE is ours to add
    uint64_t efer = rdmsr(MSR_IA32_EFER);
    if (efer & IA32_EFER_SCE) {
        kpanic("syscall_init: IA32_EFER.SCE already set");
    }
    wrmsr(MSR_IA32_EFER, efer | IA32_EFER_SCE);

    // STAR[63:48]: sysret CS/SS base, STAR[47:32]: syscall's kernel CS
    uint64_t star = ((uint64_t) X86_64_STAR_USER_BASE << 48) | ((uint64_t) X86_64_SELECTOR_KERNEL_CODE << 32);
    wrmsr(MSR_IA32_STAR, star);

    wrmsr(MSR_IA32_LSTAR, (uint64_t) syscall_entry);

    // IF must be masked on entry, or a syscall can be interrupted before it has a kernel stack
    wrmsr(MSR_IA32_FMASK, X86_64_RFLAGS_IF);

    // the kernel runs on percpu0 at all times and swapgs happens at every ring transition, both ways.
    // the exit path reads the ring out of the frame, so a switch can leave through another task's frame
    wrmsr(MSR_IA32_GS_BASE, (uint64_t) &x86_64_percpu0);
    wrmsr(MSR_IA32_KERNEL_GS_BASE, 0);  // what userland gets handed on the way out

    // kprintf("X8664", "EFER:  0x%016lx\n", rdmsr(MSR_IA32_EFER));
    // kprintf("X8664", "STAR:  0x%016lx\n", rdmsr(MSR_IA32_STAR));
    // kprintf("X8664", "LSTAR: 0x%016lx\n", rdmsr(MSR_IA32_LSTAR));
    // kprintf("X8664", "FMASK: 0x%016lx\n", rdmsr(MSR_IA32_FMASK));
    // kprintf("X8664", "KGSB:  0x%016lx\n", rdmsr(MSR_IA32_KERNEL_GS_BASE));
}

// ─── VMM Architecture Primitives ─────────────────────────────

uint64_t arch_vmm_make_pte(uint64_t phys_addr, uint64_t vmm_flags, bool is_table) {
    // x86_64: same bit layout for table and page entries (no is_table distinction needed)
    (void)is_table;
    uint64_t pte = phys_addr & ~0xFFFULL;
    if (vmm_flags & VMM_PRESENT)       pte |= (1ULL << 0);
    if (vmm_flags & VMM_WRITABLE)      pte |= (1ULL << 1);
    if (vmm_flags & VMM_USER)          pte |= (1ULL << 2);
    if (vmm_flags & VMM_CACHE_DISABLE) pte |= (1ULL << 4);
    if (vmm_flags & VMM_NO_EXECUTE)    pte |= (1ULL << 63);
    return pte;
}

bool arch_vmm_pte_is_present(uint64_t pte) {
    return (pte & (1ULL << 0)) != 0;
}

uint64_t arch_vmm_pte_get_phys(uint64_t pte) {
    return pte & 0x000FFFFFFFFFF000ULL;
}

// x86_64 has a single CR3 for both kernel and user mappings
uint64_t arch_vmm_get_kernel_root(void) {
    uint64_t val;
    asm volatile ("mov %%cr3, %0" : "=r"(val));
    return val;
}

void arch_vmm_set_user_root(uint64_t phys_addr) {
    // x86_64 has a single CR3 for both kernel and user mappings.
    asm volatile ("mov %0, %%cr3" :: "r"(phys_addr) : "memory");
}

uint64_t arch_vmm_ensure_user_root(void) {
    // Limine always leaves a valid CR3 in place on x86_64 — nothing to do.
    return arch_vmm_get_kernel_root();
}

uint64_t arch_vmm_new_context_root(uint64_t kernel_root_phys) {
    uint64_t root_phys = pmm_alloc(1);
    if (!root_phys) return 0;

    uint64_t *root_virt = pmm_phys_to_virt(root_phys);
    memset(root_virt, 0, PAGE_SIZE);

    // x86_64 has a single root (CR3) shared by kernel and user mappings: every new context
    // must clone the kernel's upper-half entries (256..511) so kernel code/data stays mapped
    // whenever this context is active.
    uint64_t *kernel_root_virt = pmm_phys_to_virt(kernel_root_phys);
    for (size_t i = 256; i < 512; i++) {
        root_virt[i] = kernel_root_virt[i];
    }

    return root_phys;
}

// #PF error code: bit 0 = present (0 = not-present fault, 1 = protection violation),
// bit 1 = write access.

bool arch_vmm_fault_is_present(uint64_t fault_code) {
    return (fault_code & (1ULL << 0)) != 0;
}

bool arch_vmm_fault_is_write(uint64_t fault_code) {
    return (fault_code & (1ULL << 1)) != 0;
}

void arch_vmm_invlpg(void *virt_addr) {
    asm volatile ("invlpg (%0)" :: "r"(virt_addr) : "memory");
}

// Timer & Interrupt

#define PIT_CMD_PORT 0x43
#define PIT_DATA_PORT  0x40
// The input clock is 1,193,182 Hz, the PIT expect a divisor of this.
#define PIT_INPUT_HZ 1193182
#define PIT_MAX_DIVISOR 0xFFFF

void arch_timer_init(uint32_t hz) {
    if (hz == 0) {
        kpanic("arch_timer_init: hz must not be zero");
    }
    uint32_t divisor = PIT_INPUT_HZ / hz;
    if (divisor == 0 || divisor > PIT_MAX_DIVISOR) {
        kpanic("arch_timer_init: hz outside the PIT's usable range");
    }
    // bit  7 6 | 5 4 | 3 2 1 | 0
    //      0 0 | 1 1 | 0 1 1 | 0     = 0x36
    //      └─┬─┘ └─┬─┘ └──┬──┘ └─ BCD/binary: 0 = binary
    //        │     │      └─ operating mode: 011 = mode 3 (square wave)
    //        │     └─ access mode: 11 = lobyte then hibyte
    //        └─ channel select: 00 = channel 0
    outb(PIT_CMD_PORT, 0x36);
    outb(PIT_DATA_PORT, divisor & 0xFF); // lowbyte
    outb(PIT_DATA_PORT, divisor >> 8); // hibyte
    // IRQ0 is wired to the PIT output
    pic_set_irq_mask(0, true);
    // we also need the local apic to be unmasked
    lapic_init();
}

void arch_irq_enable(void) {
    asm volatile ("sti" ::: "memory");
}

void arch_irq_disable(void) {
    asm volatile ("cli" ::: "memory");
}

bool arch_irq_enabled(void) {
    uint64_t flags;
    asm volatile("pushfq\n\tpopq %0" : "=r"(flags)::"memory");
    return flags & X86_64_RFLAGS_IF;
}

uint64_t arch_irq_save(void) {
    uint64_t flags;
    asm volatile("pushfq\n\tpopq %0\n\tcli" : "=r"(flags)::"memory");
    return flags;
}

void arch_irq_restore(uint64_t flags) {
    if (flags & X86_64_RFLAGS_IF) asm volatile("sti" ::: "memory");
}

// Stack

void arch_set_kernel_stack(void *stack_top) {
    idt_tss_set_rsp0(stack_top);
    // rsp0 is only ever consulted by interrupt-taken traps; `syscall` never looks at it
    x86_64_percpu0.kernel_rsp = (uint64_t) stack_top;
}

void *arch_get_stack_pointer(void) {
    uint64_t rsp;
    asm volatile ("mov %%rsp, %0" : "=r"(rsp));
    return (void *)rsp;
}

// Task

struct trap_frame *arch_task_init_frame(void *stack_top, void (*entry)(void)) {
    // no link register here, so a stray return pops this off the stack
    uint64_t *guard_slot = (uint64_t *) ((uint8_t *) stack_top - 8);
    *guard_slot = (uint64_t) task_exit_guard;

    // rsp must be 16-aligned at function entry
    struct trap_frame *frame = (struct trap_frame *) (((uint64_t) guard_slot - sizeof(struct trap_frame)) & ~0xFULL);

    memset(frame, 0, sizeof(struct trap_frame));
    frame->rip = (uint64_t) entry; // where we iretq
    frame->cs = X86_64_SELECTOR_KERNEL_CODE;
    frame->ss = X86_64_SELECTOR_KERNEL_DATA;
    frame->rflags = X86_64_RFLAGS_RESERVED_BIT1 | X86_64_RFLAGS_IF;
    frame->rsp = (uint64_t) guard_slot; // iretq pops this even at the same privilege
    frame->int_no = 0xFF; // just to be able to "see" this in a dump

    return frame;
}

struct trap_frame *arch_task_init_user_frame(void *kernel_stack_top, uint64_t user_entry, uint64_t user_stack_top) {
    struct trap_frame *frame = (struct trap_frame *) (((uint64_t) kernel_stack_top - sizeof(struct trap_frame)) & ~0xFULL);
    memset(frame, 0, sizeof(struct trap_frame));
    frame->rip = user_entry; // where we iretq
    frame->cs = X86_64_SELECTOR_USER_CODE;
    frame->ss = X86_64_SELECTOR_USER_DATA;
    frame->rflags = X86_64_RFLAGS_RESERVED_BIT1 | X86_64_RFLAGS_IF;
    frame->rsp = user_stack_top;
    frame->int_no = 0xFF;
    return frame;
}

struct switch_frame *arch_task_init_switch_frame(struct trap_frame *trap_frame) {
    struct switch_frame *frame = (struct switch_frame *) ((uint8_t *) trap_frame - sizeof(struct switch_frame));
    // ret leaves rsp 8 past the top of a 16 aligned frame, which is the alignment SysV wants at entry
    if ((uint64_t) frame & 0xF) kpanic("switch_frame is not 16 aligned");
    memset(frame, 0, sizeof(struct switch_frame));
    frame->ret = (uint64_t) task_trampoline; // where the first switch into this task lands
    return frame;
}

// ELF

uint16_t arch_wanted_elf_machine(void) {
    return EM_X86_64;
}
