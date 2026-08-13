#include "proc/kstack.h"

#include "mm/pmm.h"

static uint64_t *kstack_guard_slot(void *stack_top) {
    return (uint64_t *) ((uint8_t *) stack_top - KSTACK_SIZE);
}

void *kstack_alloc(void) {
    uint64_t phys_addr = pmm_alloc(KSTACK_PAGES);
    if (!phys_addr) return NULL;
    uint8_t *base = (uint8_t *) pmm_phys_to_virt(phys_addr);
    void *stack_top = base + KSTACK_SIZE;
    *kstack_guard_slot(stack_top) = KSTACK_GUARD;
    return stack_top;
}

void kstack_free(void *stack_top) {
    if (!stack_top) return;
    void *base = (uint8_t *) stack_top - KSTACK_SIZE;
    uint64_t phys_addr = pmm_virt_to_phys(base);
    pmm_free(phys_addr, KSTACK_PAGES);
}

bool kstack_guard_intact(void *stack_top) {
    if (!stack_top) return true;
    return *kstack_guard_slot(stack_top) == KSTACK_GUARD;
}
