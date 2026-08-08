#ifndef PROS_PMM_H
#define PROS_PMM_H

#include "stdc.h"

#include <limine.h>

#define PAGE_SIZE 4096ULL

size_t pmm_init(struct limine_hhdm_response *hhdm_response, struct limine_memmap_response *memmap_response);

void *pmm_phys_to_virt(uint64_t phys_addr);

uint64_t pmm_virt_to_phys(void *virt_addr);

uint64_t pmm_get_hhdm_offset(void);

// Highest physical address (base + length) reported by any Limine memmap entry (no matter of type)
uint64_t pmm_get_max_phys_addr(void);

uint64_t pmm_alloc(size_t count);

void pmm_free(uint64_t phys_addr, size_t count);

// Number of free pages currently on the free list (for debug/test)
size_t pmm_get_free_page_count(void);

#endif //PROS_PMM_H
