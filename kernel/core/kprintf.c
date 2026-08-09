#include "core/kprintf.h"

#include "core/console.h"
#include "arch/arch.h"

void kprintf(const char *tag, const char *format, ...) {
    printf("[%-*s] ", KPRINTF_TAG_WIDTH, tag);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

void kpanic(const char *msg) {
    kprintf("PANIC", "%s\n", msg);
    arch_halt();
}

// _putchar function that printf wants is impl here and simply sends to console
void _putchar(char character) {
    if (character == '\n') {
        console_putc('\r');
    }
    console_putc(character);
}
