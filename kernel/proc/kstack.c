#include "proc/kstack.h"

#include "mm/pmm.h"

void *kstack_alloc(void) {
    uint64_t phys_addr = pmm_alloc(KSTACK_PAGES);
    if (!phys_addr) return NULL;
    void *kstack = pmm_phys_to_virt(phys_addr);
    *((uint64_t *) kstack) = KSTACK_GUARD;
    return kstack;
}

void kstack_free(void *kstack) {
    if (!kstack) return;
    uint64_t phys_addr = pmm_virt_to_phys(kstack);
    pmm_free(phys_addr, KSTACK_PAGES);
}

void *kstack_get_top(void *kstack) {
    if (!kstack) return NULL;
    return (uint8_t *) kstack + KSTACK_SIZE;
}

bool kstack_guard_intact(void *kstack) {
    if (!kstack) return true;
    return *((uint64_t *) kstack) == KSTACK_GUARD;
}

bool kstack_contains(void *kstack, void *ptr) {
    if (!kstack || !ptr) return false;
    void *top = kstack_get_top(kstack);
    return ptr >= kstack && ptr < top;
}
