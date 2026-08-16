#ifndef PROS_X86_64_LAPIC_H
#define PROS_X86_64_LAPIC_H

#include "stdc.h"

// IA32_APIC_BASE. Bit 11 is the hardware enable, bits 12 and up hold the
// physical base of the register file. That base is 0xFEE00000 on essentially
// every machine, but it is firmware-controlled rather than architectural, so
// read it instead of assuming it (same reason aarch64 must read CNTFRQ_EL0).
#define MSR_IA32_APIC_BASE   0x1B

#define MSR_IA32_EFER            0xC0000080
#define MSR_IA32_STAR            0xC0000081
#define MSR_IA32_LSTAR           0xC0000082
#define MSR_IA32_FMASK           0xC0000084
#define MSR_IA32_KERNEL_GS_BASE  0xC0000102

inline static uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t) hi << 32) | lo;
}

inline static void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t lo = (uint32_t) value;
    uint32_t hi = (uint32_t)(value >> 32);
    asm volatile ("wrmsr" :: "c"(msr), "a"(lo), "d"(hi));
}

void lapic_init(void);

#endif //PROS_X86_64_LAPIC_H
