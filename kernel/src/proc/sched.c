#include "proc/sched.h"

#include "arch/arch.h"
#include "core/kprintf.h"
#include "core/spinlock.h"
#include "mm/vmm.h"
#include "proc/task.h"
#include "stdc.h"

static struct task *run_queue_head;
static struct task *current;                  // NULL while the scheduler thread runs
static struct task *cursor;                   // where round robin resumes looking
static struct task *scheduler_task;           // BOOT, never in the run queue
static struct switch_frame *scheduler_frame;  // its bookmark, the only frame that is not a task's
static bool need_resched;

static struct task *sched_pick_next(void) {
    if (!run_queue_head) return NULL;
    struct task *start = cursor ? cursor : run_queue_head;
    struct task *task = start;
    do {
        task = task->next;
        if (task->state == TASK_READY) {
            cursor = task;  // next round starts looking after this one
            return task;
        }
    } while (task != start);
    return NULL;  // everything is dead or blocked
}

static bool sched_any_alive(void) {
    struct task *task = run_queue_head;
    if (!task) return false;
    do {
        if (task->state != TASK_DEAD) return true;
        task = task->next;
    } while (task != run_queue_head);
    return false;
}

void sched_init(struct task *task) {
    run_queue_head = NULL;
    scheduler_task = task;  // BOOT is the scheduler, it never gets queued
}

void sched_add_task(struct task *task) {
    if (!task) return;
    if (!run_queue_head) {
        run_queue_head = task;
    } else {
        task->next = run_queue_head->next;
    }
    run_queue_head->next = task;
}

struct task *sched_get_current_task(void) {
    return current ? current : scheduler_task;
}

struct trap_frame *sched_on_trap_exit(struct trap_frame *frame) {
    // Traps are possible from the moment the vector table is installed, this is long before there is a scheduler.
    // Nothing to switch to, give the same frame back
    if (!current) return frame;

    if (!task_owns_frame(current, frame)) {
        sched_task_dump_all();
        kpanic("current task does not owns the current frame");
    }
    current->trap_frame = frame;

    if (!need_resched) return frame;
    need_resched = false;

    if (current->state == TASK_RUNNING) current->state = TASK_READY;
    sched();  // gives the cpu back. we come back here, later, as the same task

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

void sched_task_dump_all(void) {
    if (scheduler_task) task_dump(scheduler_task);  // outside the ring, nothing else would ever print it
    struct task *task = run_queue_head;
    if (!task) return;
    do {
        task_dump(task);
        task = task->next;
    } while (task != run_queue_head);
}

void sleep(void *chan, struct spinlock *lk) {
    if (!chan || !lk) kpanic("cant sleep without a chan or a lock");
    if (!current) kpanic("sleep() on the scheduler thread");
    struct task *task = current;
    task->chan = chan;
    task->state = TASK_BLOCKED;
    // drop the lock, keep irqs masked until we are off the cpu
    spinlock_unlock(lk);
    sched();  // we come back here, later, on our own stack
    task->chan = NULL;
    // we can only have been resumed by the scheduler, and it never switches unmasked
    if (arch_irq_enabled()) kpanic("resumed from sched() with interrupts enabled");
    spinlock_lock(lk);  // get our lock back, we are still masked
}

void wakeup(void *chan) {
    if (!chan) return;
    struct task *task = run_queue_head;
    if (!task) return;
    do {
        if (task->state == TASK_BLOCKED && task->chan == chan) task->state = TASK_READY;
        task = task->next;
    } while (task != run_queue_head);
}

void sched(void) {
    if (arch_irq_enabled()) kpanic("sched() with interrupts enabled");
    if (!scheduler_frame) kpanic("sched() with no scheduler to go back to");
    arch_task_switch_to(&current->switch_frame, scheduler_frame);
    // we come back here, on our own stack, whenever someone picks us again
}

void scheduler(void) {
    // the scheduler runs masked, arch_idle() is the only place that opens the window
    arch_irq_disable();
    while (true) {
        struct task *next = sched_pick_next();
        if (next) {
            if (!task_stack_intact(next)) kpanic("kernel stack overflow on the task we are about to run");
            current = next;
            next->state = TASK_RUNNING;
            next->switch_count++;
            arch_set_kernel_stack(next->kernel_stack_top);
            vmm_switch_context(next->vmm_context);
            arch_task_switch_to(&scheduler_frame, next->switch_frame);
            // we are the scheduler again
            scheduler_task->switch_count++;
            if (current != next) kpanic("came back from a task we did not dispatch");
            current = NULL;
            if (!task_stack_intact(next)) kpanic("kernel stack overflow on the task we just left");
        } else if (sched_any_alive()) {
            // nothing ready, but something is blocked: wait for the interrupt that frees it
            arch_idle();
        } else {
            break;
        }
    }
    kprintf("K", "All done here, shutting down.\n");
    arch_shutdown();
}
