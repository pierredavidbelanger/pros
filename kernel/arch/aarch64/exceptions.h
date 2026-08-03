#ifndef PROS_AARCH64_EXCEPTIONS_H
#define PROS_AARCH64_EXCEPTIONS_H

#include <stdint.h>

struct aarch64_registers {
    uint64_t x[31];
    uint64_t sp;
    uint64_t elr;
    uint64_t spsr;
    uint64_t esr;
    uint64_t far;
    uint64_t vector_type;
};

void aarch64_exception_handler(struct aarch64_registers *regs);

#endif //PROS_AARCH64_EXCEPTIONS_H
