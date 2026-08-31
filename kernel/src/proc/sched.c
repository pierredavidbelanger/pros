#include "proc/sched.h"

#include "arch/arch.h"
#include "core/kprintf.h"
#include "mm/vmm.h"
#include "proc/task.h"

static bool need_resched;

static struct task *run_queue_head;
static struct task *current;

// where whoever is playing scheduler parked itself, the only frame that is not a task's
static struct switch_frame *scheduler_frame;

static struct task *sched_pick_next(void) {
    struct task *next = current->next;
    while (next->state == TASK_DEAD) {
        if (next == current) kpanic("no runnable tasks");
        next = next->next;
    }
    return next;
}

void sched_init(void) {
    run_queue_head = NULL;
}

void sched_add_task(struct task *task) {
    if (!task) return;
    if (!run_queue_head) {
        // we are given the first task, but sched_on_trap_exit has not schedule it by itself yet
        // we need to call arch_set_kernel_stack manually here and make it the current
        run_queue_head = task;
        task->state = TASK_RUNNING;
        arch_set_kernel_stack(task->kernel_stack_top);
        current = task;
    } else {
        task->next = run_queue_head->next;
    }
    run_queue_head->next = task;
}

struct task *sched_get_current_task(void) {
    return current;
}

struct trap_frame *sched_on_trap_exit(struct trap_frame *frame) {
    // Traps are possible from the moment the vector table is installed, this is long before there is a scheduler.
    // Nothing to switch to, give the same frame back
    if (!current) return frame;

    if (!task_owns_frame(current, frame)) {
        task_dump_all();
        kpanic("current task does not owns the current frame");
    }
    current->trap_frame = frame;

    if (!need_resched) return frame;
    need_resched = false;

    struct task *next = sched_pick_next();
    if (next == current) return frame;

    if (!task_stack_intact(current)) kpanic("kernel stack overflow on the current task");
    if (!task_stack_intact(next)) kpanic("kernel stack overflow on the next task");

    if (current->state != TASK_DEAD) current->state = TASK_READY;
    next->state = TASK_RUNNING;
    next->switch_count++;
    current = next;

    arch_set_kernel_stack(current->kernel_stack_top);
    vmm_switch_context(current->vmm_context);
    return current->trap_frame;
}

void sched_on_timer_tick(void) {
    need_resched = true;
}

void sched_exit_current(void) {
    // current is flagged dead
    current->state = TASK_DEAD;
    // but we need a resched for it to happen for real
    need_resched = true;
}

bool sched_only_current_is_alive(void) {
    struct task *first = sched_get_current_task();
    if (!first) return false;
    struct task *task = first;
    while (true) {
        if (task != first && task->state != TASK_DEAD) return false;
        task = task->next;
        if (task == first) break;
    }
    return true;
}

void sched(void) {
    if (arch_irq_enabled()) kpanic("sched() with interrupts enabled");
    arch_task_switch_to(&current->switch_frame, scheduler_frame);
    // we come back here, on our own stack, whenever someone picks us again
}

void sched_switch_to(struct task *task) {
    struct task *prev = current;
    current = task;
    task->state = TASK_RUNNING;
    task->switch_count++;
    arch_task_switch_to(&scheduler_frame, task->switch_frame);
    current = prev;  // C1 leaves this NULL: while the scheduler runs there is no current task
}

void scheduler(void) {
    // TODO actually do the scheduling
    while (true) {
        arch_idle();
    }
    arch_shutdown();
}
