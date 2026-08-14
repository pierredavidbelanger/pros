#include "proc/task.h"

#include "arch/arch.h"
#include "core/kprintf.h"
#include "mm/heap.h"
#include "mm/vmm.h"
#include "proc/kstack.h"
#include "proc/sched.h"

static uint64_t next_pid = 1;
static const char *task_state_names[] = {"READY", "RUNNING"};

struct task *task_init_boot(void) {
    struct task *task = kcalloc(1, sizeof(struct task));
    if (!task) return NULL;
    task->pid = 0;
    task->state = TASK_RUNNING;
    snprintf(task->name, TASK_NAME_SIZE, "task0");
    task->kernel_stack_top = arch_get_stack_pointer(); // a point inside Limine stack, not its top
    return task;
}

struct task *task_create(const char *name, void (*entry)(void)) {
    struct task *task = kcalloc(1, sizeof(struct task));
    if (!task) return NULL;
    task->pid = next_pid++;
    task->state = TASK_READY;
    snprintf(task->name, TASK_NAME_SIZE, "%s", name);
    task->kernel_stack_base = kstack_alloc();
    if (!task->kernel_stack_base) {
        kfree(task);
        return NULL;
    }
    task->kernel_stack_top = kstack_get_top(task->kernel_stack_base);
    // task->ctx = vmm_create_context();
    // if (!task->ctx) {
        // kfree(task);
        // kstack_free(task->kstack);
        // return NULL;
    // }
    task->frame = arch_task_init_frame(task->kernel_stack_top, entry);
    return task;
}

void task_destroy(struct task *task) {
    if (!task) return;
    if (task->kernel_stack_base) {
        kstack_free(task->kernel_stack_base);
    }
    if (task->ctx) {
        vmm_destroy_context(task->ctx);
    }
    kfree(task);
}

bool task_owns_frame(const struct task *task, const struct trap_frame *frame) {
    if (!task || !frame) return false;
    if (!task->kernel_stack_base) return true;
    if (!kstack_contains(task->kernel_stack_base, (void *) frame)) return false;
    return true;
}

void task_exit_guard(void) {
    kpanic("kernel thread returned");
}

bool task_stack_intact(const struct task *task) {
    if (!task) return false;
    if (!task->kernel_stack_base) return true;
    if (!kstack_guard_intact(task->kernel_stack_base)) return false;
    return true;
}

void task_dump(struct task *task) {
    if (!task) return;

    const char *state = task_state_names[task->state];

    // Task 0 runs on the stack handed by Limine: no base, no extent, no guard.
    if (!task->kernel_stack_base) {
        kprintf("TASK", "pid %zu  %s  %s  switch_count %zu  boot stack, sp was 0x%016lx, extent unknown, guard n/a\n",
            task->pid, task->name, state, task->switch_count,
            (uint64_t)task->kernel_stack_top);
        return;
    }

    uint64_t free = 0;
    uint64_t total = 0;
    kstack_get_usage(task->kernel_stack_base, &free, &total);
    kprintf("TASK", "pid %zu  %s  %s  switch_count %zu  kstack 0x%016lx-0x%016lx  free %zu/%zu  guard %s\n",
        task->pid, task->name, state, task->switch_count,
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
