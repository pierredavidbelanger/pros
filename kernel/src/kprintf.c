#include "kprintf.h"

#include "fb.h"

#include <printf.h>

// Halt and catch fire function.
void khcf(void) {
    for (;;) {
#if defined (__x86_64__)
        asm ("hlt");
#elif defined (__aarch64__) || defined (__riscv)
        asm ("wfi");
#elif defined (__loongarch64)
        asm ("idle 0");
#endif
    }
}

void kprintf(const char *fmt, ...) {
    char buf[512];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    fb_draw_string(1, 1, buf, 0x00FFFFFF, 0x00000000);
    va_end(va);
}

// printf lib want this function defined, we dont use it for now.
void _putchar(char character) {
}
