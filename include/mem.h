#ifndef PROS_MEM_H
#define PROS_MEM_H

#include <stddef.h>

struct kmem_page {
    struct kmem_page *next;
};

void kmem_init(char *base, size_t size);

void *kmem_alloc(void);

void kmem_free(void *);

#endif //PROS_MEM_H
