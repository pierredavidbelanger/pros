#ifndef PROS_X86_64_IDT_H
#define PROS_X86_64_IDT_H

#include "stdc.h"

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  attributes;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

struct trap_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, error_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

#define X86_64_SELECTOR_KERNEL_CODE 0x08        // gdt.kernel_code, from tss_init()
#define X86_64_SELECTOR_KERNEL_DATA 0x10        // gdt.kernel_data
#define X86_64_SELECTOR_USER_DATA   0x33        // gdt.user_data (0x30) | RPL 3
#define X86_64_SELECTOR_USER_CODE   0x3b        // gdt.user_code (0x38) | RPL 3

#define X86_64_STAR_USER_BASE       0x28

#define X86_64_RFLAGS_RESERVED_BIT1 (1ULL << 1) // always 1
#define X86_64_RFLAGS_IF            (1ULL << 9) // interrupts enabled

void idt_init(void);

void idt_tss_set_rsp0(void *rsp0);

#endif //PROS_X86_64_IDT_H
