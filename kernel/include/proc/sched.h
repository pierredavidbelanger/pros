#ifndef PROS_SCHED_H
#define PROS_SCHED_H

#include "arch/arch.h"
#include "proc/task.h"

void sched_init(void);

void sched_add_task(struct task *task);

struct task *sched_get_current_task(void);

struct trap_frame *sched_on_trap_exit(struct trap_frame *frame);

void sched_on_timer_tick(void);

void sched_exit_current(void);

#endif  // PROS_SCHED_H
