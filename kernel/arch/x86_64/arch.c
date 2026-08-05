#include "arch.h"

void arch_init(void) {
}

void arch_halt(void) {
    for (;;) {
        asm volatile ("cli; hlt");
    }
}
