#include "core/console.h"

#include "core/earlycon.h"
#include "core/fb.h"

#include "stdc.h"

static bool earlycon_active = false;

void console_init(void) {
    earlycon_init();
    earlycon_active = true;
    // limine leaves the cursor at row 1, so our first line would land on theirs
    // we dont clear, their output is worth reading when a boot goes wrong
    console_putc('\n');
    console_putc('\n');
}

void console_putc(char c) {
    if (earlycon_active) {
        earlycon_putc(c);
    }
    if (fb_is_active()) {
        fb_terminal_print_char(c);
    }
}
