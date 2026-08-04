#ifndef PROS_HEAP_H
#define PROS_HEAP_H

#include <stddef.h>

void heap_init();

void *kmalloc(size_t size);

void kfree(void *virt_addr);

void *kcalloc(size_t num, size_t size);

#endif //PROS_HEAP_H
