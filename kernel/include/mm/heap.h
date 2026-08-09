#ifndef PROS_HEAP_H
#define PROS_HEAP_H

#include "stdc.h"

void heap_init();

void *kmalloc(size_t size);

void *kcalloc(size_t num, size_t size);

void *krealloc(void *virt_addr, size_t size);

void kfree(void *virt_addr);

#endif //PROS_HEAP_H
