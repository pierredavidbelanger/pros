#include "proc/sched.h"

#include "arch/arch.h"
#include "core/kprintf.h"
#include "proc/task.h"
#include "proc/kstack.h"

static bool need_resched;

static struct task *current;

static struct task *sched_pick_next(void) {
    return current;
}

void sched_init(void) {
    current = task_init_boot();
    if (!current) kpanic("cant create boot task");
    arch_set_kernel_stack(current->kernel_stack_top);
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

    if (!kstack_guard_intact(next->kernel_stack_top)) kpanic("kernel stack overflow");

    arch_set_kernel_stack(next->kernel_stack_top);
    current = next;

    return next->frame;
}

void sched_on_timer_tick(void) {
    need_resched = true;
}
