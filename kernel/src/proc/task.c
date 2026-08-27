#include "proc/task.h"

#include "arch/arch.h"
#include "core/kprintf.h"
#include "core/syscalls.h"
#include "errno.h"
#include "fs/vfs/file.h"
#include "mm/heap.h"
#include "mm/vmm.h"
#include "proc/kstack.h"
#include "proc/sched.h"

static uint64_t next_pid = 0;

static const char *task_state_names[] = {"DEAD", "READY", "RUNNING", "BLOCKED"};

// stdin/stdout/stderr share one open file, exactly like a real shell's 0/1/2
static int task_open_std_fds(struct task *task) {
    struct vfs_node *console = vfs_lookup("/dev/console");
    if (!console) return -ENOENT;

    struct file *f = file_open_node(console, O_RDWR);
    if (!f) return -ENOMEM;

    task->fds[0] = f;

    task->fds[1] = file_ref(f);
    if (!task->fds[1]) return -EBADF;

    task->fds[2] = file_ref(f);
    if (!task->fds[2]) return -EBADF;

    return 0;
}

static struct task *task_inner_create(char const *name, void *kernel_stack_top, struct vmm_context *ctx) {
    struct task *task = kcalloc(1, sizeof(struct task));
    if (!task) return NULL;
    task->pid = next_pid++;
    task->state = TASK_READY;
    task->name = name;
    // are we provided with a stack we dont own, otherwise create one
    if (kernel_stack_top) {
        task->kernel_stack_top = kernel_stack_top;
    } else {
        task->kernel_stack_base = kstack_alloc();
        if (!task->kernel_stack_base) {
            task_destroy(task);
            return NULL;
        }
        task->kernel_stack_top = kstack_get_top(task->kernel_stack_base);
    }
    // are we provided with a context we dont own, otherwise create one
    if (ctx) {
        task->vmm_context = ctx;
    } else {
        task->vmm_context = vmm_create_context();
        if (!task->vmm_context) {
            task_destroy(task);
            return NULL;
        }
    }
    if (task_open_std_fds(task) != 0) {
        task_destroy(task);
        return NULL;
    }
    return task;
}

struct task *task_init_boot(void) {
    struct task *task = task_inner_create("BOOT", arch_get_stack_pointer(), vmm_kernel_context);
    if (!task) return NULL;
    return task;
}

struct task *task_create(const char *name, void (*entry)(void)) {
    struct task *task = task_inner_create(name, NULL, vmm_kernel_context);
    if (!task) return NULL;
    task->trap_frame = arch_task_init_frame(task->kernel_stack_top, entry);
    return task;
}

struct task *task_create_user(const char *name, struct vmm_context *ctx, uint64_t user_entry, uint64_t user_stack_top) {
    struct task *task = task_inner_create(name, NULL, ctx);
    if (!task) return NULL;
    task->trap_frame = arch_task_init_user_frame(task->kernel_stack_top, user_entry, user_stack_top);
    return task;
}

void task_destroy(struct task *task) {
    if (!task) return;
    // close all files
    for (int fd = 0; fd < MAX_OPEN_FILES; fd++) {
        if (task->fds[fd]) {
            file_unref(task->fds[fd]);
            task->fds[fd] = NULL;
        }
    }
    // do we have a ctx and do we own it
    if (task->vmm_context && task->vmm_context != vmm_kernel_context) {
        vmm_destroy_context(task->vmm_context);
    }
    // do we have a stack that we own
    if (task->kernel_stack_base) {
        kstack_free(task->kernel_stack_base);
    }
    kfree(task);
}

bool task_owns_frame(const struct task *task, const struct trap_frame *frame) {
    if (!task || !frame) return false;
    if (!task->kernel_stack_base) return true;
    if (!kstack_contains(task->kernel_stack_base, (void *)frame)) return false;
    return true;
}

bool task_stack_intact(const struct task *task) {
    if (!task) return false;
    if (!task->kernel_stack_base) return true;
    if (!kstack_guard_intact(task->kernel_stack_base)) return false;
    return true;
}

void task_exit_guard(void) {
    kpanic("kernel thread returned");
}

void task_dump(struct task *task) {
    if (!task) return;

    const char *state = task_state_names[task->state];

    // Task 0 runs on the stack handed by Limine: no base, no extent, no guard.
    if (!task->kernel_stack_base) {
        kprintf("TASK", "pid %zu  %s  %s  switch_count %zu  ctx 0x%016lx  boot stack, sp was 0x%016lx, extent unknown, guard n/a\n",
                task->pid, task->name, state, task->switch_count,
                (uint64_t)task->vmm_context,
                (uint64_t)task->kernel_stack_top);
        return;
    }

    uint64_t free = 0;
    uint64_t total = 0;
    kstack_get_usage(task->kernel_stack_base, &free, &total);
    kprintf("TASK", "pid %zu  %s  %s  switch_count %zu  ctx 0x%016lx  kstack 0x%016lx-0x%016lx  free %zu/%zu  guard %s\n",
            task->pid, task->name, state, task->switch_count,
            (uint64_t)task->vmm_context,
            (uint64_t)task->kernel_stack_base, (uint64_t)task->kernel_stack_top,
            free, total,
            kstack_guard_intact(task->kernel_stack_base) ? "ok" : "GONE");
}

void task_dump_all(void) {
    struct task *first = sched_get_current_task();
    if (!first) return;
    struct task *task = first;
    while (true) {
        task_dump(task);
        task = task->next;
        if (task == first) break;
    }
}

int64_t sys_getpid(void) {
    return sched_get_current_task()->pid;
}

int64_t sys_exit(int status) {
    (void)status;  // nothing collects it, no parent and no wait until phase 7
    sched_exit_current();
    return 0;
}
