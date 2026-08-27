#ifndef PROS_SCHED_H
#define PROS_SCHED_H

#include "arch/arch.h"
#include "proc/task.h"
#include "stdc.h"

void sched_init(void);

void sched_add_task(struct task *task);

struct task *sched_get_current_task(void);

struct trap_frame *sched_on_trap_exit(struct trap_frame *frame);

void sched_on_timer_tick(void);

void sched_exit_current(void);

bool sched_only_current_is_alive(void);

// hand the cpu back to the scheduler thread, irqs must already be masked
// we return here, later, on our own stack, when someone picks us again.
// in other words, calling sched() will yeld into scheduler()
void sched(void);

// the scheduler thread:
// run what is ready, idle while anything is alive, never returns
void scheduler(void);

#endif  // PROS_SCHED_H
