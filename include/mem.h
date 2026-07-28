#ifndef PROS_MEM_H
#define PROS_MEM_H

#include <stddef.h>
#include <stdint.h>

struct kmem_page {
    struct kmem_page *next;
};

size_t kmem_init(uintptr_t heap_start, uintptr_t heap_end);

void *kmem_alloc(void);

void kmem_free(void *ptr);

#endif //PROS_MEM_H
