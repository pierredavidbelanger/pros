#ifndef PROS_SCHED_H
#define PROS_SCHED_H

#include "arch/arch.h"
#include "core/spinlock.h"
#include "proc/task.h"

void sched_init(struct task *task);

void sched_add_task(struct task *task);

struct task *sched_get_current_task(void);

struct trap_frame *sched_on_trap_exit(struct trap_frame *frame);

void sched_on_timer_tick(void);

void sched_exit_current(void);

void sched_task_dump_all(void);

// block on chan, lk is released while we sleep and held again on the way out
void sleep(void *chan, struct spinlock *lk);

// wakeup everyone TASK_BLOCKED on chan
void wakeup(void *chan);

// hand the cpu back to the scheduler thread, irqs must already be masked
// we return here, later, on our own stack, when someone picks us again.
// in other words, calling sched() will yeld into scheduler()
void sched(void);

// the scheduler thread:
// run what is ready, idle while anything is alive, never returns
void scheduler(void);

#endif  // PROS_SCHED_H
