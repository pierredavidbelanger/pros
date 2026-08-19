#ifndef PROS_SERIAL_H
#define PROS_SERIAL_H

#include "stdc.h"

// makes serial_putc() work
// needs only the manual MMIO mapping (aarch64) / port I/O (x86_64)
void serial_init_put(void);

// makes serial_getc()/RX work
// needs the interrupt controller (and, on aarch64, PMM/VMM) alive
void serial_init_get(void);

// true while the RX FIFO still has a byte the ISR hasn't drained
bool serial_rx_ready(void);

void serial_putc(char c);

char serial_getc(void);

#endif // PROS_SERIAL_H
