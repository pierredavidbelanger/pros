#include "mm/heap.h"

#include "mm/pmm.h"
#include "core/memory.h"
#include "core/kprintf.h"

#define HEAP_ALIGNMENT 16
#define HEAP_ALIGN_UP(size, align) (((size) + (align) - 1) & ~((align) - 1))

struct heap_block {
    size_t size; // acutal usable size (so sizeof(struct heap_block) must be excluded)
    bool is_free;
    struct heap_block *next;
};

struct heap_block *heap_block_list;

static void heap_block_coalesce(struct heap_block *block) {
    // coalesce the next following blocks
    // if its next block exists at all, is free and is contiguous
    void *block_end = (void *) block + sizeof(struct heap_block) + block->size;
    while (block->next && block->next->is_free && block_end == (void *) block->next) {
        // it is safe to coalesce
        block->size += sizeof(struct heap_block) + block->next->size;
        block->next = block->next->next;
        // block has grow, recalculate its end
        block_end = (void *) block + sizeof(struct heap_block) + block->size;
    }
}

static void heap_block_split(struct heap_block *block, size_t size) {
    // has NOT enough space (at least header + HEAP_ALIGNMENT) to be halved, dont do it
    if (block->size < size + sizeof(struct heap_block) + HEAP_ALIGNMENT) return;

    // the remainder is right after the block header + the requested size
    struct heap_block *remainder = (struct heap_block *) ((void *) block + sizeof(struct heap_block) + size);

    // the remainder size if whats left after removing a header + the requested size
    remainder->size = block->size - sizeof(struct heap_block) - size;
    remainder->is_free = true;
    // insert remainder between block and its next
    remainder->next = block->next;
    block->next = remainder;

    // block is then properly sized to the requested size
    block->size = size;
    
    // the remainder inherits the end from block,
    // remainder is free, and it may now be contiguous with a free block
    heap_block_coalesce(remainder);
}

void heap_init() {
    // create the head of the list
    uint64_t phys_addr = pmm_alloc(1);
    if (!phys_addr) {
        kpanic("Cant alloc the first PMM page");
    }
    heap_block_list = pmm_phys_to_virt(phys_addr);
    heap_block_list->size = PAGE_SIZE - sizeof(struct heap_block);
    heap_block_list->is_free = true;
    heap_block_list->next = NULL;
}

void *kmalloc(size_t size) {
    // will we cause an overflow, in the align up below or in the page count math further down
    if (size > SIZE_MAX - sizeof(struct heap_block) - PAGE_SIZE - HEAP_ALIGNMENT) return NULL;

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
        if (!phys_addr) return NULL;
        block = pmm_phys_to_virt(phys_addr);
        block->size = pages * PAGE_SIZE - sizeof(struct heap_block);
        block->is_free = true;
        block->next = heap_block_list;
        // is the new head
        heap_block_list = block;
    }

    // split if possible
    heap_block_split(block, size);

    // block is now in use
    block->is_free = false;

    // return the address right after the header, that is effectively of the the requested size (or more)
    return (struct heap_block *) ((void *) block + sizeof(struct heap_block));
}

void *kcalloc(size_t num, size_t size) {
    // will we cause an overflow
    if (size != 0 && num > SIZE_MAX / size) return NULL;

    size_t total_size = num * size;

    // delegate to kmalloc, and zero the memory
    void *ptr = kmalloc(total_size);
    if (!ptr) return NULL;
    memset(ptr, 0, total_size);

    return ptr;
}

void *krealloc(void *virt_addr, size_t size) {

    // will we cause an overflow
    if (size > SIZE_MAX - (HEAP_ALIGNMENT - 1)) return NULL;

    // simple case
    // 1- krealloc(NULL, n) behaves as kmalloc(n)
    // 2- krealloc(p, 0) behaves as kfree(p) and returns NULL
    // then we have 3 cases
    // 3- size already fits in our block, just return the ptr, its ok
    // 4- size dont fit, but we can grow in place by absorbing the next block
    // 5- size dont fit, we cannot grow in place, we fall back to alloc-copy-free

    // case 1
    if (!virt_addr) {
        return kmalloc(size);
    }

    // case 2
    if (!size) {
        kfree(virt_addr);
        return NULL;
    }

    // align up requested size to HEAP_ALIGNMENT
    size = HEAP_ALIGN_UP(size, HEAP_ALIGNMENT);

    struct heap_block *block = virt_addr - sizeof(struct heap_block);

    // case 3
    if (size <= block->size) {
        // hand off left over
        heap_block_split(block, size);
        return virt_addr;
    }

    // case 4
    heap_block_coalesce(block);
    if (size <= block->size) {
        // hand off left over
        heap_block_split(block, size);
        return virt_addr;
    }

    // case 5
    void *new_addr = kmalloc(size);
    if (!new_addr) return NULL;
    memcpy(new_addr, virt_addr, block->size);
    kfree(virt_addr);
    return new_addr;
}

void kfree(void *virt_addr) {
    if (!virt_addr) return;

    struct heap_block *block = virt_addr - sizeof(struct heap_block);
    block->is_free = true;

    // Coalesce, otherwise memory get fragmented over time
    heap_block_coalesce(block);
}
