#include "kprintf.h"

#include "fb.h"

#include <printf.h>

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
    va_list va;
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
}
