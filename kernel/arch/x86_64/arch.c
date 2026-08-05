#include "arch.h"
#include "gdt.h"
#include "idt.h"

void arch_init(void) {
    gdt_init();
    idt_init();
}

void arch_cli(void) {
    asm volatile ("cli");
}

void arch_sti(void) {
    asm volatile ("sti");
}

void arch_pause(void) {
    asm volatile ("pause");
}

void arch_halt(void) {
    for (;;) {
        asm volatile ("cli; hlt");
    }
}

// Architecture VMM impl

// on x86_64 there is only CR3, so arch_vmm_get/set_kernel_root is using arch_vmm_get/set_user_root

uint64_t arch_vmm_get_kernel_root(void) {
    return arch_vmm_get_user_root();
}

void arch_vmm_set_kernel_root(uint64_t phys_addr) {
    arch_vmm_set_user_root(phys_addr);
}

uint64_t arch_vmm_get_user_root(void) {
    uint64_t val;
    asm volatile ("mov %%cr3, %0" : "=r"(val));
    return val;
}

void arch_vmm_set_user_root(uint64_t phys_addr) {
    asm volatile ("mov %0, %%cr3" :: "r"(phys_addr) : "memory");
}

uint64_t arch_vmm_get_fault_addr(void) {
    uint64_t val;
    asm volatile ("mov %%cr2, %0" : "=r"(val));
    return val;
}

void arch_vmm_invlpg(void *virt_addr) {
    asm volatile ("invlpg (%0)" :: "r"(virt_addr) : "memory");
}

bool arch_vmm_get_flags(uint64_t pte, uint64_t flags) {
    if ((flags & VMM_PRESENT) && !(pte & (1ULL << 0))) return false;
    if ((flags & VMM_WRITABLE) && !(pte & (1ULL << 1))) return false;
    if ((flags & VMM_USER) && !(pte & (1ULL << 2))) return false;
    if ((flags & VMM_CACHE_DISABLE) && !(pte & (1ULL << 4))) return false;
    if ((flags & VMM_NO_EXECUTE) && !(pte & (1ULL << 63))) return false;
    return true;
}

uint64_t arch_vmm_set_flags(uint64_t pte, uint64_t flags) {
    if (flags & VMM_PRESENT) pte |= (1ULL << 0);
    if (flags & VMM_WRITABLE) pte |= (1ULL << 1);
    if (flags & VMM_USER) pte |= (1ULL << 2);
    if (flags & VMM_CACHE_DISABLE) pte |= (1ULL << 4);
    if (flags & VMM_NO_EXECUTE) pte |= (1ULL << 63);
    return pte;
}
