#ifndef PROS_ARCH_H
#define PROS_ARCH_H

#include "stdc.h"

// the layout is architecture-specific and not visible here
struct trap_frame;

void arch_init(void);

void arch_cpu_relax(void);

void arch_halt(void);

// Cleanly power off the machine
void arch_shutdown(void);

// VMM

// Build a hardware PTE from physical address + abstract VMM flags.
// Set is_table=true for intermediate (non-leaf) table descriptors.
uint64_t arch_vmm_make_pte(uint64_t phys_addr, uint64_t vmm_flags, bool is_table);

// Test whether a hardware PTE is valid/present
bool arch_vmm_pte_is_present(uint64_t pte);

// Extract the physical address from a hardware PTE (mask off flag bits)
uint64_t arch_vmm_pte_get_phys(uint64_t pte);

// Get the physical base address of the kernel's root page table, and set the currently
// active low-half (user) root.
// No arch_vmm_get_user_root()/arch_vmm_set_kernel_root() needed.
// "the user root" and "the kernel root" is the same on x86_64, and arch_vmm_ensure_user_root() below is the only place that distinction ever mattered, so each arch do it directly
uint64_t arch_vmm_get_kernel_root(void);
void arch_vmm_set_user_root(uint64_t phys_addr);

// Ensure the current low-half root register holds a valid root table,
// allocating and installing a fresh one if the bootloader didn't already set one up (needed on aarch64).
// Return the physical address of the root table.
uint64_t arch_vmm_ensure_user_root(void);

// Build a fresh, zeroed root page-table for a new address space (context), with whatever
// architecture-specific setup is needed so it correctly sees the kernel's existing mappings
// (e.g. cloning shared entries on a single-root architecture; nothing to do on a dual-root
// one). `kernel_root_phys` is the kernel's own root table (arch_vmm_get_kernel_root()).
// Returns the physical address of the new root, or 0 on allocation failure.
uint64_t arch_vmm_new_context_root(uint64_t kernel_root_phys);

// Decode the fault_code passed to vmm_handle_page_fault() (x86_64 #PF error code, or the
// raw AArch64 ESR_EL1 value): distinguishes a legitimate not-present (demand-pageable) fault
// from a real protection violation, and whether the faulting access was a write.
bool arch_vmm_fault_is_present(uint64_t fault_code);
bool arch_vmm_fault_is_write(uint64_t fault_code);

// Invalidate TLB entry for a specific virtual address
void arch_vmm_invlpg(void *virt_addr);

// Timer & Interrupt

void arch_timer_init(uint32_t hz);

void arch_irq_enable(void);
void arch_irq_disable(void);

uint64_t arch_irq_save(void);           // mask, return the previous state
void arch_irq_restore(uint64_t flags);  // put it back exactly as it was

// Stack

// Tell the CPU where the kernel stack for the next trap from a lower privilege level is.
// Called on every context switch, before returning to a different task frame.
void arch_set_kernel_stack(void *stack_top);

// Where the stack pointer is right now.
void *arch_get_stack_pointer(void);

// Task

struct trap_frame *arch_task_init_frame(void *stack_top, void (*entry)(void));

struct trap_frame *arch_task_init_user_frame(void *kernel_stack_top, uint64_t user_entry, uint64_t user_stack_top);

// ELF

uint16_t arch_wanted_elf_machine();

#endif  // PROS_ARCH_H
