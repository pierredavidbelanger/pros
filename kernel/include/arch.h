#ifndef PROS_ARCH_H
#define PROS_ARCH_H

#include <stdint.h>

void arch_init(void);

// Disable CPU interrupts.
void arch_cli(void);

// Enable CPU interrupts.
void arch_sti(void);

// Low-power spin/pause hint for tight loops.
void arch_pause(void);

// Halt the CPU in an idle loop.
void arch_halt(void);

#endif //PROS_ARCH_H
