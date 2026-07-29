#include "mem.h"

#include "string.h"

#include <stdint.h>

#define PAGE_SIZE 4096

struct kmem_page {
    struct kmem_page *next;
};

static struct kmem_page *kmem_list;

size_t kmem_page_init(uintptr_t heap_start, uintptr_t heap_end) {
    kmem_list = 0;
    size_t pages = 0;
    // iterate each page and free it (doing so will initialise the free list kmem_list)
    for (uintptr_t page = heap_start; page + PAGE_SIZE <= heap_end; page += PAGE_SIZE) {
        kmem_page_free((void *) page);
        pages++;
    }
    return pages;
}

void *kmem_page_alloc(void) {
    void *ptr = kmem_list;
    kmem_list = kmem_list->next;
    memset(ptr, 0, PAGE_SIZE);
    return ptr;
}

void kmem_page_free(void *ptr) {
    struct kmem_page *page = ptr;
    page->next = kmem_list;
    kmem_list = page;
}
