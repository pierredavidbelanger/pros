#include "mem.h"

#include <stdint.h>

#define PAGE_SIZE 4096

static struct kmem_page *kmem_list;

size_t kmem_init(uintptr_t heap_start, uintptr_t heap_end) {
    kmem_list = 0;
    size_t pages = 0;
    // iterate each page and free it (doing so will initialise the free list kmem_list)
    for (uintptr_t page = heap_start; page + PAGE_SIZE <= heap_end; page += PAGE_SIZE) {
        kmem_free((void *) page);
        pages++;
    }
    return pages;
}

void *kmem_alloc(void) {
    return 0;
}

void kmem_free(void *ptr) {
    struct kmem_page *page = ptr;
    page->next = kmem_list;
    kmem_list = page;
}
