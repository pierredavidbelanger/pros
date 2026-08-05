#include "kprintf.h"

#include "arch.h"

#include <printf.h>

void kprintf(const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
}

void kpanic(const char *msg) {
    kprintf("PANIC: %s\n", msg);
    arch_halt();
}

// _putchar function that printf want is impl here and simply send to UART
void _putchar(char character) {
}
