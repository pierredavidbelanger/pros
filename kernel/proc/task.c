#include "proc/task.h"

#include "arch/arch.h"
#include "core/kprintf.h"
#include "mm/heap.h"
#include "mm/vmm.h"
#include "proc/kstack.h"

static const char *task_state_names[] = {"READY", "RUNNING"};

struct task *task_init_boot(void) {
    struct task *task0 = kcalloc(1, sizeof(struct task));
    if (task0 == NULL) return NULL;
    task0->pid = 0;
    task0->state = TASK_RUNNING;
    task0->kernel_stack_top = arch_get_stack_pointer(); // a point inside Limine stack, not its top
    return task0;
}

void task_dump(struct task *task) {
    if (!task) return;

    const char *state = task_state_names[task->state];

    // Task 0 runs on the stack handed by Limine: no base, no extent, no guard.
    if (!task->kernel_stack) {
        kprintf("TASK", "pid %zu  %s  boot stack, sp was 0x%016lx, extent unknown, guard n/a\n", task->pid, state, (uint64_t)task->kernel_stack_top);
        return;
    }

    kprintf("TASK", "pid %zu  %s  kstack 0x%016lx-0x%016lx  guard %s\n", task->pid, state, (uint64_t)task->kernel_stack, (uint64_t)task->kernel_stack_top, kstack_guard_intact(task->kernel_stack_top) ? "ok" : "GONE");
}
