#ifndef PROS_KSTACK_H
#define PROS_KSTACK_H

#include "mm/pmm.h"

#include "stdc.h"

// It has to hold a trap frame plus the deepest kernel call chain.
// vmm_free_table_recursive is well, recursive, so that chain is not easy to determine
#define KSTACK_PAGES 4
#define KSTACK_SIZE  (KSTACK_PAGES * PAGE_SIZE)

// Sits in the lowest 8 bytes of every stack.
// An overflow reaches it before it reaches anything else, we guess it will panic instead
#define KSTACK_GUARD 0x5041474544414544ULL

// Returns the stack top: the highest address, one past the end of the allocation.
// Stacks grow down, so that is where a stack pointer starts.
void *kstack_alloc(void);

// Free the stack (from the stack top, as we reverse what kstack_alloc did).
void kstack_free(void *stack_top);

bool kstack_guard_intact(void *stack_top);

#endif //PROS_KSTACK_H
