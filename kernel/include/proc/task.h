#ifndef PROS_TASK_H
#define PROS_TASK_H

#include "arch/arch.h"
#include "fs/vfs/file.h"
#include "mm/vmm.h"
#include "stdc.h"

#define TASK_DEAD 0
#define TASK_READY 1
#define TASK_RUNNING 2
#define TASK_BLOCKED 3

struct task {
    uint64_t pid;
    int state;
    char const *name;                   // for the dump essentially
    void *kernel_stack_base;            // base, for freeing and for the guard check
    void *kernel_stack_top;             // what arch_set_kernel_stack() gets
    struct trap_frame *trap_frame;      // where we re-enter what got interrupted, rewritten on every kernel entry
    struct switch_frame *switch_frame;  // where the scheduler resumes us, valid only while we are not running
    struct vmm_context *vmm_context;    // its address space
    uint64_t switch_count;              // how many times this task has been switched to
    struct file *fds[MAX_OPEN_FILES];   // per process FDs, FD are shared and ref-counted
    void *chan;                         // what a TASK_BLOCKED task is sleeping on, or NULL
    struct task *next;
};

struct task *task_init_boot(void);

struct task *task_create(const char *name, void (*entry)(void));

// build a task that starts at EL0/ring3,
// user_entry/user_stack_top are user addresses, never kernel ones
struct task *task_create_user(const char *name, struct vmm_context *ctx, uint64_t user_entry, uint64_t user_stack_top);

void task_destroy(struct task *task);

bool task_owns_frame(const struct task *task, const struct trap_frame *frame);

bool task_stack_intact(const struct task *task);

void task_exit_guard(void);

// where the first switch into a brand new task lands, never called directly
void task_trampoline(void);

void task_dump(struct task *task);

void task_dump_all(void);

#endif  // PROS_TASK_H
