#include "core/timer.h"

// I am the Mad Hatter
static volatile uint64_t tick;

void timer_tick(void) {
    tick++;
}

uint64_t timer_get_ticks(void) {
    return tick;
}
