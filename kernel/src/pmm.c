#include "pmm.h"

#include "kprintf.h"

#include <stddef.h>

#define IS_ALIGNED(x, align) (((x) & ((align) - 1)) == 0)
#define ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))
#define ALIGN_DOWN(x, align) ((x) & ~((align) - 1))

struct pmm_node {
    struct pmm_node *next;
};

static struct pmm_node *free_list_head = NULL;

static uint64_t hhdm_offset = 0;

static void *phys_to_virt(uint64_t phys_addr) {
    return (void *) (phys_addr + hhdm_offset);
}

static uint64_t virt_to_phys(void *virt_addr) {
    return (uint64_t) virt_addr - hhdm_offset;
}

size_t pmm_init(struct limine_hhdm_response *hhdm_response, struct limine_memmap_response *memmap_response) {
    if (hhdm_response == NULL) {
        kpanic("Cannot map memory blocks without access to HHDM.");
    }
    if (memmap_response == NULL || memmap_response->entry_count < 1) {
        kpanic("Cannot map memory blocks of zero length.");
    }

    hhdm_offset = hhdm_response->offset;

    size_t count = 0;

    for (uint64_t i = 0; i < memmap_response->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap_response->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        uint64_t aligned_base = ALIGN_UP(entry->base, PAGE_SIZE);
        uint64_t aligned_top = ALIGN_DOWN(entry->base + entry->length, PAGE_SIZE);
        if (aligned_base >= aligned_top) {
            continue;
        }

        for (uint64_t phys_addr = aligned_base; phys_addr < aligned_top; phys_addr += PAGE_SIZE) {
            pmm_free(phys_addr);
            count++;
        }
    }

    return count;
}

uint64_t pmm_alloc(void) {
    if (free_list_head == NULL) {
        kpanic("OOM!");
    }
    struct pmm_node *node = free_list_head;
    free_list_head = node->next;
    uint64_t phys_addr = virt_to_phys(node);
    return phys_addr;
}

void pmm_free(uint64_t phys_addr) {
    if (phys_addr == 0) {
        return;
    }
    struct pmm_node *node = phys_to_virt(phys_addr);
    node->next = free_list_head;
    free_list_head = node;
}
