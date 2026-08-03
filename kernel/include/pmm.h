#ifndef PROS_PMM_H
#define PROS_PMM_H

#include <stdint.h>
#include <stddef.h>

#include <limine.h>

#define PAGE_SIZE 4096ULL

size_t pmm_init(struct limine_hhdm_response *hhdm_response, struct limine_memmap_response *memmap_response);

uint64_t pmm_alloc(void);

void pmm_free(uint64_t phys_addr);

#endif //PROS_PMM_H
