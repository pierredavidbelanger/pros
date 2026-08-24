#include "pic.h"

#include "io.h"

void pic_init(void) {
    // Remap the PIC
    //
    // as i understand, the default PIC config at boot is
    // IRQ0,1,3,5,6 are mapped to vector 8,9,11,13,14
    // which are used by the CPU for #DF, #NP, #GP, #PF
    //
    // set up cascading mode
    outb(PIC_MASTER_CMD, PIC_ICW1_INIT | PIC_ICW1_ICW4);
    outb(PIC_SLAVE_CMD, PIC_ICW1_INIT | PIC_ICW1_ICW4);
    // Tell the master its vector offset
    outb(PIC_MASTER_DATA, PIC_VECTOR_BASE);
    // Tell the slave its vector offset
    outb(PIC_SLAVE_DATA, PIC_VECTOR_BASE + PIC_IRQS_PER_CHIP);
    // Tell the master that he has a slave on IRQ2
    outb(PIC_MASTER_DATA, 1 << PIC_CASCADE_IRQ);
    // Tell the slave he is IRQ2
    outb(PIC_SLAVE_DATA, PIC_CASCADE_IRQ);
    // Enabled 8086 mode
    outb(PIC_MASTER_DATA, PIC_ICW4_8086);
    outb(PIC_SLAVE_DATA, PIC_ICW4_8086);
    // Mask every line
    outb(PIC_MASTER_DATA, PIC_MASK_BLOCK_ALL);
    outb(PIC_SLAVE_DATA, PIC_MASK_BLOCK_ALL);
}

// Flip one bit of one chip's mask register (OCW1)
// The register is read/write, so read-modify-write to leave the other seven lines untouched
static void pic_write_mask_bit(uint16_t data_port, uint8_t bit, bool enabled) {
    uint8_t line = (uint8_t)(1u << bit);
    uint8_t mask = inb(data_port);
    // 1 means masked/blocked, 0 means unmasked/allowed — so enabling clears the bit
    outb(data_port, enabled ? (uint8_t)(mask & ~line) : (uint8_t)(mask | line));
}

void pic_set_irq_mask(uint8_t irq, bool enabled) {
    // the pair has no such line
    if (irq >= PIC_IRQ_COUNT) return;
    if (irq < PIC_IRQS_PER_CHIP) {
        pic_write_mask_bit(PIC_MASTER_DATA, irq, enabled);
        return;
    }
    pic_write_mask_bit(PIC_SLAVE_DATA, (uint8_t)(irq - PIC_IRQS_PER_CHIP), enabled);
    bool any_slave_enabled = inb(PIC_SLAVE_DATA) != PIC_MASK_BLOCK_ALL;
    pic_write_mask_bit(PIC_MASTER_DATA, PIC_CASCADE_IRQ, any_slave_enabled);
}
