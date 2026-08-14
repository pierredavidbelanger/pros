#include "proc/sched.h"

#include "arch/arch.h"
#include "core/kprintf.h"
#include "proc/task.h"

static bool need_resched;

static struct task *run_queue_head;
static struct task *current;

static struct task *sched_pick_next(void) {
    return current->next;
}

void sched_init(void) {
    run_queue_head = NULL;
    struct task *task0 = task_init_boot();
    if (!task0) kpanic("cant create boot task");
    run_queue_head = task0;
    task0->next = task0;
    arch_set_kernel_stack(task0->kernel_stack_top);
    current = task0;
}

void sched_add_task(struct task *task) {
    task->next = run_queue_head->next;
    run_queue_head->next = task;
}

struct task *sched_get_current_task(void) {
    return current;
}

struct trap_frame *sched_on_trap_exit(struct trap_frame *frame) {
    // Traps are possible from the moment the vector table is installed, this is long before there is a scheduler.
    // Nothing to switch to, give the same frame back
    if (!current) return frame;

    current->frame = frame;

    if (!need_resched) return frame;
    need_resched = false;

    struct task *next = sched_pick_next(); // returns current today
    if (next == current) return frame;

    if (!task_stack_intact(current)) kpanic("kernel stack overflow on the current task");
    if (!task_stack_intact(next)) kpanic("kernel stack overflow on the next task");

    current->state = TASK_READY;
    next->state = TASK_RUNNING;
    next->switch_count++;

    arch_set_kernel_stack(next->kernel_stack_top);
    current = next;

    return next->frame;
}

void sched_on_timer_tick(void) {
    need_resched = true;
}
