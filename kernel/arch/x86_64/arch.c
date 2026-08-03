#include "arch.h"
#include "gdt.h"
#include "idt.h"

void arch_init(void) {
    gdt_init();
    idt_init();
}

void arch_cli(void) {
    asm volatile ("cli");
}

void arch_sti(void) {
    asm volatile ("sti");
}

void arch_pause(void) {
    asm volatile ("pause");
}

void arch_halt(void) {
    for (;;) {
        asm volatile ("cli; hlt");
    }
}
