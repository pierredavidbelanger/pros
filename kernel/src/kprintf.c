#include "kprintf.h"

#include "arch.h"
#include "fb.h"

#include <printf.h>

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
