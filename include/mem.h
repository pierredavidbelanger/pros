#ifndef PROS_MEM_H
#define PROS_MEM_H

#include <stddef.h>
#include <stdint.h>

size_t kmem_page_init(uintptr_t heap_start, uintptr_t heap_end);

void *kmem_page_alloc(void);

void kmem_page_free(void *ptr);

#endif //PROS_MEM_H
