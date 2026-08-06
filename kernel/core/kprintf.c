#include "core/kprintf.h"

#include "core/console.h"
#include "arch/arch.h"

#include <printf.h>

#include "stdc.h"

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

// _putchar function that printf wants is impl here and simply sends to console
void _putchar(char character) {
    if (character == '\n') {
        console_putc('\r');
    }
    console_putc(character);
}
