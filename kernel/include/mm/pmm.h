#ifndef PROS_PMM_H
#define PROS_PMM_H

#include "stdc.h"

#define PAGE_SIZE 4096ULL

void pmm_init();

uint64_t pmm_claim(uint64_t memmap_type);

void *pmm_phys_to_virt(uint64_t phys_addr);

uint64_t pmm_virt_to_phys(void *virt_addr);

// Highest physical address (base + length) reported by any Limine memmap entry (no matter of type)
uint64_t pmm_get_max_phys_addr(void);

uint64_t pmm_alloc(size_t count);

void pmm_free(uint64_t phys_addr, size_t count);

// Number of free pages currently on the free list (for debug/test)
size_t pmm_get_free_page_count(void);

#endif //PROS_PMM_H
