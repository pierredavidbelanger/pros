#ifndef PROS_ARCH_H
#define PROS_ARCH_H

#include <stdint.h>
#include <stdbool.h>

void arch_init(void);

void arch_putc(char c);

void arch_halt(void);

// Cleanly power off the machine
void arch_shutdown(void);

#endif //PROS_ARCH_H
