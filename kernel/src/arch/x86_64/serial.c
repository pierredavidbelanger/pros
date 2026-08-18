#include "core/serial.h"

#include "io.h"
#include "pic.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, inb(COM1 + 1) | 0x01); // Enable RX
    outb(COM1 + 3, 0x80); // Enable DLAB (set baud rate divisor)
    outb(COM1 + 0, 0x03); // Set divisor to 3 (lo byte) 38400 baud
    outb(COM1 + 1, 0x00); //                  (hi byte)
    outb(COM1 + 3, 0x03); // 8 bits, no parity, one stop bit
    outb(COM1 + 2, 0xC7); // Enable FIFO, clear them, 14-byte threshold
    outb(COM1 + 4, 0x0B); // IRQs enabled, RTS/DSR set
    pic_set_irq_mask(4, true); // Unmask IRQ 4 (hard linked to COM1 & COM3 for some reason?)
}

void serial_putc(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0) {
        // Wait until Transmit holding register is empty
    }
    outb(COM1, (uint8_t)c);
}

char serial_getc(void) {
    return inb(COM1);
}
