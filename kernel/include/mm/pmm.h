#ifndef PROS_PMM_H
#define PROS_PMM_H

#include <stdint.h>
#include <stddef.h>

#include <limine.h>

#define PAGE_SIZE 4096ULL

size_t pmm_init(struct limine_hhdm_response *hhdm_response, struct limine_memmap_response *memmap_response);

void *pmm_phys_to_virt(uint64_t phys_addr);

uint64_t pmm_virt_to_phys(void *virt_addr);

uint64_t pmm_alloc(size_t count);

void pmm_free(uint64_t phys_addr, size_t count);

#endif //PROS_PMM_H
