#include "core/timer.h"

#include "proc/sched.h"

// I am the Mad Hatter
static volatile uint64_t tick;

void timer_tick(void) {
    tick++;
    sched_on_timer_tick();
}

uint64_t timer_get_ticks(void) {
    return tick;
}
