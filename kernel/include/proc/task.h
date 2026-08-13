#ifndef PROS_TASK_H
#define PROS_TASK_H

#include "arch/arch.h"
#include "mm/vmm.h"

#include "stdc.h"

#define TASK_READY   0
#define TASK_RUNNING 1

struct task {
    uint64_t pid;
    int state;                 // TASK_RUNNING / TASK_READY for now; the rest earn their way in
    void *kernel_stack;        // base, for freeing and for the guard check
    void *kernel_stack_top;    // what arch_set_kernel_stack() gets
    struct trap_frame *frame;  // valid only while suspended
    struct vmm_context *ctx;   // already exists — vmm_create_context()
    struct task *next;
};

struct task *task_init_boot(void);

void task_dump(struct task *task);

#endif //PROS_TASK_H
