#ifndef PROS_AARCH64_TIMER_H
#define PROS_AARCH64_TIMER_H

#include "stdc.h"

void arm_timer_init(uint32_t hz);

// Push the deadline forward by one interval.
void arm_timer_rearm(void);

#endif  // PROS_AARCH64_TIMER_H
