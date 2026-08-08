#include "mm/pmm.h"

#include "core/kprintf.h"

#define PMM_ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))
#define PMM_ALIGN_DOWN(x, align) ((x) & ~((align) - 1))

struct pmm_node {
    struct pmm_node *next;
};

// TODO: having a prepend list (stack) here render the pointer maths somewhat hard to follow, we should have a real sorted list
static struct pmm_node *free_list_head = NULL;

static uint64_t hhdm_offset = 0;
static uint64_t max_phys_addr = 0;

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

        // Track the highest physical address across every entry type (not just USABLE),
        // so MMIO/reserved regions above RAM are still covered by the HHDM extent.
        uint64_t entry_top = entry->base + entry->length;
        if (entry_top > max_phys_addr) {
            max_phys_addr = entry_top;
        }

        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        uint64_t aligned_base = PMM_ALIGN_UP(entry->base, PAGE_SIZE);
        uint64_t aligned_top = PMM_ALIGN_DOWN(entry->base + entry->length, PAGE_SIZE);
        if (aligned_base >= aligned_top) {
            continue;
        }

        for (uint64_t phys_addr = aligned_base; phys_addr < aligned_top; phys_addr += PAGE_SIZE) {
            pmm_free(phys_addr, 1);
            count++;
        }
    }

    return count;
}

void *pmm_phys_to_virt(uint64_t phys_addr) {
    return (void *) (phys_addr + hhdm_offset);
}

uint64_t pmm_virt_to_phys(void *virt_addr) {
    return (uint64_t) virt_addr - hhdm_offset;
}

uint64_t pmm_get_hhdm_offset(void) {
    return hhdm_offset;
}

uint64_t pmm_get_max_phys_addr(void) {
    return max_phys_addr;
}

uint64_t pmm_alloc(size_t count) {
    bool found = false;
    struct pmm_node *prev_node = NULL;
    struct pmm_node *start_node = free_list_head;
    while (start_node != NULL) {
        struct pmm_node *curr_node = start_node;
        bool contiguous = true;
        for (size_t actual = 0; actual < count - 1; actual++) {
            // here we check that curr_node - PAGE_SIZE == curr_node->next
            // because page are prepended, so the order is reversed
            contiguous = (void *) curr_node - PAGE_SIZE == (void *) curr_node->next;
            if (!contiguous) {
                break;
            }
            curr_node = curr_node->next;
        }
        if (contiguous) {
            found = true;
            break;
        }
        prev_node = start_node;
        start_node = start_node->next;
    }
    if (!found) {
        kpanic("No enough memory available");
    }
    struct pmm_node *end_node = start_node->next;
    for (size_t actual = 0; actual < count - 1; actual++) {
        end_node = end_node->next;
    }
    if (prev_node == NULL) {
        free_list_head = end_node;
    } else {
        prev_node->next = end_node;
    }
    // Return the lowest (base) physical address of the contiguous block
    // again because page are prepended, so the order is reversed
    uint64_t phys_addr = pmm_virt_to_phys(start_node) - ((count - 1) * PAGE_SIZE);
    return phys_addr;
}

void pmm_free(uint64_t phys_addr, size_t count) {
    if (phys_addr == 0) {
        return;
    }
    uint64_t next_addr = phys_addr;
    for (int i = 0; i < count; i++) {
        struct pmm_node *node = pmm_phys_to_virt(next_addr);
        node->next = free_list_head;
        free_list_head = node;
        next_addr += PAGE_SIZE;
    }
}

size_t pmm_get_free_page_count(void) {
    // yeah, we have no "tail", so must iterate from the head
    size_t count = 0;
    for (struct pmm_node *node = free_list_head; node != NULL; node = node->next) {
        count++;
    }
    return count;
}
