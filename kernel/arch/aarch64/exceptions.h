#ifndef PROS_AARCH64_EXCEPTIONS_H
#define PROS_AARCH64_EXCEPTIONS_H

#include "stdc.h"

struct aarch64_registers {
    uint64_t x[31];       // x0..x30
    uint64_t sp;          // Original SP before trap
    uint64_t elr;         // ELR_EL1 (return address)
    uint64_t spsr;        // SPSR_EL1
    uint64_t esr;         // ESR_EL1 (exception syndrome)
    uint64_t far;         // FAR_EL1 (fault address)
    uint64_t vector_type; // Which vector slot (0..15)
};

void aarch64_exception_handler(struct aarch64_registers *regs);

#endif //PROS_AARCH64_EXCEPTIONS_H
