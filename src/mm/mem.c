#include <mm/mem.h>
#include <lib/string.h>

#include <stdint.h>

#define SCRATCH_BUF_SIZE 1024*1024
static uint8_t buf[SCRATCH_BUF_SIZE];

struct kpage {
    struct kpage *next;
};

static struct kpage *kpages;

size_t kscratch_bss_buf_size() {
    return SCRATCH_BUF_SIZE;
}

void *kscratch_bss_buf() {
    return buf;
}

size_t kpinit(uintptr_t heap_start, uintptr_t heap_end) {
    kpages = 0;
    size_t pages = 0;
    // iterate each page and free it (doing so will initialise the free list kmem_list)
    for (uintptr_t page = heap_start; page + KPAGE_SIZE <= heap_end; page += KPAGE_SIZE) {
        kpfree((void *) page);
        pages++;
    }
    return pages;
}

void *kpmalloc(void) {
    void *ptr = kpages;
    kpages = kpages->next;
    memset(ptr, 0, KPAGE_SIZE);
    return ptr;
}

void kpfree(void *ptr) {
    struct kpage *page = ptr;
    page->next = kpages;
    kpages = page;
}
