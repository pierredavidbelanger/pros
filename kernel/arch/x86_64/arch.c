#include "arch.h"

extern void serial_init();

void arch_init(void) {
    serial_init();
}

void arch_halt(void) {
    for (;;) {
        asm volatile ("cli; hlt");
    }
}

static inline void outw(uint16_t port, uint16_t val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

void arch_shutdown(void) {
    // QEMU q35 ACPI PM1a_CNT_BLK poweroff (SLP_TYPa | SLP_EN)
    outw(0x604, 0x2000);
    // Fallback: QEMU debug-exit port
    outw(0x501, 0x00);
    // Fallback: halt if poweroff is not supported by hardware
    arch_halt();
}
