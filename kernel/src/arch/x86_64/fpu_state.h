#ifndef PROS_X86_64_FPU_STATE_H
#define PROS_X86_64_FPU_STATE_H

#include "stdc.h"

// the fxsave image, laid out by the hardware, we only ever name the two control words
struct fpu_state {
    uint16_t fcw;  // x87 control word
    uint16_t fsw;
    uint8_t ftw;
    uint8_t pad0;
    uint16_t fop;
    uint64_t fip;
    uint64_t fdp;
    uint32_t mxcsr;  // sse control and status
    uint32_t mxcsr_mask;
    uint8_t st[8][16];     // st0-st7, 80 bits each in a 16 byte slot
    uint8_t xmm[16][16];   // xmm0-xmm15
    uint8_t reserved[96];  // pads to the 512 fxsave writes
} __attribute__((aligned(16)));

#define X86_64_FCW_DEFAULT 0x37F     // x87 exceptions masked, extended precision
#define X86_64_MXCSR_DEFAULT 0x1F80  // sse exceptions masked, round to nearest

_Static_assert(offsetof(struct fpu_state, mxcsr) == 24, "fxsave puts mxcsr at 24");
_Static_assert(sizeof(struct fpu_state) == 512, "fxsave writes exactly 512 bytes");

#endif  // PROS_X86_64_FPU_STATE_H
