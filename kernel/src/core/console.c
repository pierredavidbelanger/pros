#include "core/console.h"

#include "core/serial.h"
#include "core/fb.h"

#include "stdc.h"

void console_init(void) {
    // limine leaves the cursor at row 1, so our first line would land on theirs
    // we dont clear, their output is worth reading when a boot goes wrong
    console_putc('\n');
    console_putc('\n');
}

void console_putc(char c) {
    serial_putc(c);
    if (fb_is_active()) {
        fb_terminal_print_char(c);
    }
}
