#include "core/kprintf.h"

#include "core/console.h"
#include "arch/arch.h"

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
