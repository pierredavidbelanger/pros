#include "core/console.h"

#include "core/earlycon.h"
#include "core/fb.h"

#include "stdc.h"

static bool earlycon_active = false;

void console_init(void) {
    earlycon_init();
    earlycon_active = true;
    // clear the terminal so kernel output starts on a clean line instead of overlapping
    const char *clear_seq = "\033[2J\033[H";
    for (const char *p = clear_seq; *p; p++) {
        console_putc(*p);
    }
}

void console_putc(char c) {
    if (earlycon_active) {
        earlycon_putc(c);
    }
    if (fb_is_active()) {
        fb_terminal_print_char(c);
    }
}
