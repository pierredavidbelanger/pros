#include <kernel/kernel.h>
#include <asm/arch.h>
#include <lib/string.h>

#include <stdarg.h>

int kprintf(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    uart_puts(buf);
    return len;
}

void kpanic(const char *msg) {
    kprintf("KERNEL PANIC: %s\n", msg);
    while (1) {
        arch_idle();
    }
}
