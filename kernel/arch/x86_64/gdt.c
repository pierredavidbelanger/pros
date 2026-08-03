#include "gdt.h"

static struct gdt_entry gdt_entries[3];
static struct gdt_ptr   gdt_pointer;

extern void gdt_flush(uint64_t gdt_ptr_addr);

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_entries[num].base_low    = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high   = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low   = (limit & 0xFFFF);
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;

    gdt_entries[num].granularity |= gran & 0xF0;
    gdt_entries[num].access      = access;
}

void gdt_init(void) {
    gdt_pointer.limit = (sizeof(struct gdt_entry) * 3) - 1;
    gdt_pointer.base  = (uint64_t)&gdt_entries;

    // 0: Null Descriptor
    gdt_set_gate(0, 0, 0, 0, 0);

    // 1: Kernel 64-bit Code Segment (0x08)
    // Access: Present (0x80) | Ring 0 (0x00) | Code Exec/Read (0x1A) = 0x9A
    // Flags: 64-bit long mode (0x20), Granularity 4K (0x80) = 0xA0
    gdt_set_gate(1, 0, 0xFFFFF, 0x9A, 0xA0);

    // 2: Kernel 64-bit Data Segment (0x10)
    // Access: Present (0x80) | Ring 0 (0x00) | Data Read/Write (0x12) = 0x92
    // Flags: Default (0x00)
    gdt_set_gate(2, 0, 0xFFFFF, 0x92, 0x00);

    // Load GDT and reload CS / DS / ES / SS registers
    gdt_flush((uint64_t)&gdt_pointer);
}
