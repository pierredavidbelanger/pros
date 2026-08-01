#include <kernel/kernel.h>
#include <asm/arch.h>
#include <drivers/of/fdt.h>
#include <mm/mem.h>
#include <lib/string.h>

#include <stdarg.h>

extern char _kernel_rx_start[];
extern char _kernel_rx_end[];
extern char _kernel_rw_start[];
extern char _kernel_rw_end[];
extern char _stack_bottom[];
extern char _stack_top[];
extern char _heap_start[];

/*
 * Architecture-agnostic main kernel entry point.
 */
int kmain(const uint32_t hart_id, const void *fdt_ptr) {
    kprintf("PjErOS\n");
    kprintf("%s\n", ARCH_NAME);
    kprintf("Hardware thread %d\n", hart_id);

    uintptr_t ram_base = 0;
    size_t ram_size = 0;
    kprintf("Parse the Flattened Device Tree at %p to get memory infos\n", fdt_ptr);
    fdt_dump(fdt_ptr);
    kpanic("Stop here");
    if (fdt_get_memory(fdt_ptr, &ram_base, &ram_size) == 0) {
        kprintf("Found %uMB based at %p\n", ram_size / 1024 / 1024, ram_base);
    } else {
        kpanic("Error parsing Flattened Device Tree");
    }

    uintptr_t heap_start = (uintptr_t) _heap_start;
    uintptr_t heap_end = ram_base + ram_size;
    kprintf("Init heap memory pages free list between %p and %p\n", heap_start, heap_end);
    size_t pages = kpinit(heap_start, heap_end);
    kprintf("%u pages of heap memory init between %p and %p\n", pages, heap_start, heap_end);

    kprintf("Setup VMM root table\n");
    uintptr_t *kpt = vmm_create_root_table();
    kprintf("Setup VMM root table at %p\n", kpt);
    kprintf("Map kernel VMM page for %p to %p (text + rodata) as RX\n", _kernel_rx_start, _kernel_rx_end);
    for (uintptr_t paddr = (uintptr_t) _kernel_rx_start; paddr < (uintptr_t) _kernel_rx_end; paddr += KPAGE_SIZE) {
        vmm_map_page(kpt, paddr, paddr, 1, 0, 1);
    }
    kprintf("Map kernel VMM page for %p to %p (data + bss) as RW\n", _kernel_rw_start, _kernel_rw_end);
    for (uintptr_t paddr = (uintptr_t) _kernel_rw_start; paddr < (uintptr_t) _kernel_rw_end; paddr += KPAGE_SIZE) {
        vmm_map_page(kpt, paddr, paddr, 1, 1, 0);
    }
    kprintf("Map kernel VMM page for %p to %p (stack) as RW\n", _stack_bottom, _stack_top);
    for (uintptr_t paddr = (uintptr_t) _stack_bottom; paddr < (uintptr_t) _stack_top; paddr += KPAGE_SIZE) {
        vmm_map_page(kpt, paddr, paddr, 1, 1, 0);
    }
    kprintf("Map kernel VMM page for %p to %p (heap) as RW\n", heap_start, heap_end);
    for (uintptr_t paddr = heap_start; paddr < heap_end; paddr += KPAGE_SIZE) {
        vmm_map_page(kpt, paddr, paddr, 1, 1, 0);
    }
    uintptr_t uart_paddr = uart_addr();
    kprintf("Map kernel VMM page for %p (UART) as RW\n", uart_paddr);
    vmm_map_page(kpt, uart_paddr, uart_paddr, 1, 1, 0);
    kprintf("Enable VMM\n");
    vmm_enable(kpt);

#if defined(__riscv)
    kprintf("Testing RISC-V trap handler...\n");
    asm volatile("ecall");
    kprintf("Returned from ecall successfully!\n");
#endif
#if defined(__arm__)
    kprintf("Testing ARM trap handler...\n");
    asm volatile("svc #0");
    kprintf("Returned from ARM SVC successfully!\n");
#endif

    kprintf("Wait for interrupt\n");
    while (1) {
        arch_idle();
    }

    return 0;
}
