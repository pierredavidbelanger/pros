#include "console.h"
#include "earlycon.h"
#include <stdbool.h>

static bool earlycon_active = false;

void console_init(void) {
    earlycon_init();
    earlycon_active = true;
}

void console_putc(char c) {
    if (earlycon_active) {
        earlycon_putc(c);
    }
}

void console_puts(const char *s) {
    while (*s) {
        console_putc(*s++);
    }
}
