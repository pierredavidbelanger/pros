#ifndef PROS_ARCH_H
#define PROS_ARCH_H

#include <stdint.h>
#include <stdbool.h>

void arch_init(void);

// Halt the CPU in an idle loop.
void arch_halt(void);

#endif //PROS_ARCH_H
