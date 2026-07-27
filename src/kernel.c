#include "arch.h"
#include "uart.h"

/* 
 * Architecture-agnostic main kernel entry point.
 */
int kmain(void) {
    uart_puts("Hello from Bare-Metal ");
    uart_puts(ARCH_NAME);
    uart_puts("!\n");

    while (1) {
        arch_idle();
    }

    return 0;
}
