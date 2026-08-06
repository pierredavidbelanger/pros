#include "mm/heap.h"

#include "mm/pmm.h"

#define HEAP_ALIGNMENT 16
#define HEAP_ALIGN_UP(size, align) (((size) + (align) - 1) & ~((align) - 1))

struct heap_block {
    size_t size; // acutal usable size (so sizeof(struct heap_block) must be excluded)
    bool is_free;
    struct heap_block *next;
};

struct heap_block *heap_block_list;

void heap_init() {
    // create the head of the list
    uint64_t phys_addr = pmm_alloc(1);
    heap_block_list = pmm_phys_to_virt(phys_addr);
    heap_block_list->size = PAGE_SIZE - sizeof(struct heap_block);
    heap_block_list->is_free = true;
    heap_block_list->next = NULL;
}

void *kmalloc(size_t size) {
    // align up requested size to HEAP_ALIGNMENT
    size = HEAP_ALIGN_UP(size, HEAP_ALIGNMENT);
    // first search an existing free block that has enough space for the requested size
    struct heap_block *block = NULL;
    for (struct heap_block *search = heap_block_list; search; search = search->next) {
        if (search->is_free && search->size >= size) {
            block = search;
            break;
        }
    }
    // a free block does not exist, we create one, with the correct number of pages
    if (!block) {
        // ensure the integer division returns at least 1
        size_t pages = (sizeof(struct heap_block) + size + PAGE_SIZE - 1) / PAGE_SIZE;
        uint64_t phys_addr = pmm_alloc(pages);
        block = pmm_phys_to_virt(phys_addr);
        block->size = pages * PAGE_SIZE - sizeof(struct heap_block);
        block->is_free = true;
        block->next = heap_block_list;
        // is the new head
        heap_block_list = block;
    }
    // if the found (or new) block has enough space (at least header + HEAP_ALIGNMENT) to be halved, do it
    if (block->size >= size + sizeof(struct heap_block) + HEAP_ALIGNMENT) {
        // the remainder is right after the block header + the requested size
        struct heap_block *remainder = (struct heap_block *) ((void *) block + sizeof(struct heap_block) + size);
        // the remainder size if whats left after removing a header + the requested size
        remainder->size = block->size - sizeof(struct heap_block) - size;
        remainder->is_free = true;
        // remainder's next block is the the found (or new) block, effectively inserting itself between the two
        remainder->next = block->next;
        // the found (or new) block is then properly sized to the requested size
        block->size = size;
        // the remainder immediately follow the the found (or new) block
        block->next = remainder;
    }
    block->is_free = false;
    // return the address right after the header, that is effectively of the the requested size (or more)
    return (struct heap_block *) ((void *) block + sizeof(struct heap_block));
}

void *kcalloc(size_t num, size_t size) {
    // TODO
    return NULL;
}

void kfree(void *virt_addr) {
    // TODO
}
