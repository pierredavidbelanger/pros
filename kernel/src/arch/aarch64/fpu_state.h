#ifndef PROS_AARCH64_FPU_STATE_H
#define PROS_AARCH64_FPU_STATE_H

#define FPU_STATE_OFF_Q(n) ((n) * 16)  // q0 first, 16 bytes each
#define FPU_STATE_OFF_FPCR 512
#define FPU_STATE_OFF_FPSR 520
#define FPU_STATE_SIZE 528

#ifndef __ASSEMBLER__

#include "stdc.h"

// q0-q31 then the two control registers, one 8 byte slot each so a single stp covers both
struct fpu_state {
    uint8_t q[32][16];
    uint64_t fpcr;
    uint64_t fpsr;
} __attribute__((aligned(16)));

_Static_assert(offsetof(struct fpu_state, fpcr) == FPU_STATE_OFF_FPCR, "fpu_state.h offsets are stale");
_Static_assert(sizeof(struct fpu_state) == FPU_STATE_SIZE, "fpu.S moves by FPU_STATE_SIZE");

#endif  // __ASSEMBLER__

#endif  // PROS_AARCH64_FPU_STATE_H
