#ifndef PROS_MEM_H
#define PROS_MEM_H

#include <stddef.h>
#include <stdint.h>

#define KPAGE_SIZE 4096

size_t kscratch_bss_buf_size();

void *kscratch_bss_buf();

size_t kpinit(uintptr_t heap_start, uintptr_t heap_end);

void *kpmalloc(void);

void kpfree(void *ptr);

#endif //PROS_MEM_H
