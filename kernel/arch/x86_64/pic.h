#ifndef PROS_PIC_H
#define PROS_PIC_H

// 8259A PIC pair. Even port is command, odd port is data;
#define PIC_MASTER_CMD   0x20 // ICW1, OCW2 (EOI), OCW3
#define PIC_MASTER_DATA  0x21 // ICW2, ICW3, ICW4, then OCW1 (mask)
#define PIC_SLAVE_CMD    0xA0
#define PIC_SLAVE_DATA   0xA1

#define PIC_ICW1_INIT    0x10
#define PIC_ICW1_ICW4    0x01
#define PIC_ICW4_8086    0x01

#define PIC_EOI          0x20 // OCW2: non-specific end of interrupt

// First CPU vector the pair delivers, i.e. the ICW2 value.
#define PIC_VECTOR_BASE  0x20

#define PIC_IRQS_PER_CHIP 8
#define PIC_IRQ_COUNT     16
#define PIC_CASCADE_IRQ   2  // master line the slave is wired to

#define PIC_MASK_BLOCK_ALL 0xFF // means masked/blocked
// #define PIC_MASK_ALLOW_ALL 0x00 // means unmasked/allowed

void pic_init(void);

#endif //PROS_PIC_H
