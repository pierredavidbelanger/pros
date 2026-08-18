#include "core/console.h"

#include "core/serial.h"
#include "core/fb.h"

#include "stdc.h"

static bool serial_active = false;

void console_init(void) {
    serial_init();
    serial_active = true;
    // limine leaves the cursor at row 1, so our first line would land on theirs
    // we dont clear, their output is worth reading when a boot goes wrong
    console_putc('\n');
    console_putc('\n');
}

void console_putc(char c) {
    if (serial_active) {
        serial_putc(c);
    }
    if (fb_is_active()) {
        fb_terminal_print_char(c);
    }
}
