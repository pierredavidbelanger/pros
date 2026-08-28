#ifndef PROS_AARCH64_TRAP_FRAME_H
#define PROS_AARCH64_TRAP_FRAME_H

#define TRAP_FRAME_OFF_X(n) ((n) * 8)

#define TRAP_FRAME_OFF_SP 248
#define TRAP_FRAME_OFF_ELR 256
#define TRAP_FRAME_OFF_SPSR 264
#define TRAP_FRAME_OFF_ESR 272
#define TRAP_FRAME_OFF_FAR 280
#define TRAP_FRAME_OFF_VECTOR_TYPE 288

// sizeof(struct trap_frame) is 296, but the stubs move sp by 304.
// The extra 8 bytes are because AArch64 check for 16-byte alignment
#define TRAP_FRAME_SIZE 304

#ifndef __ASSEMBLER__

#include "stdc.h"

struct trap_frame {
    uint64_t x[31];        // x0..x30
    uint64_t sp;           // Original SP before trap
    uint64_t elr;          // ELR_EL1 (return address)
    uint64_t spsr;         // SPSR_EL1
    uint64_t esr;          // ESR_EL1 (exception syndrome)
    uint64_t far;          // FAR_EL1 (fault address)
    uint64_t vector_type;  // Which vector slot (0..15)
};

// vectors.S read/write into this struct through the constant offsets
// assert to ties the two together
_Static_assert(offsetof(struct trap_frame, x) == TRAP_FRAME_OFF_X(0), "trap_frame.h offsets are stale");
_Static_assert(offsetof(struct trap_frame, x[1]) == TRAP_FRAME_OFF_X(1), "trap_frame.h offsets are stale");
_Static_assert(offsetof(struct trap_frame, sp) == TRAP_FRAME_OFF_SP, "trap_frame.h offsets are stale");
_Static_assert(offsetof(struct trap_frame, elr) == TRAP_FRAME_OFF_ELR, "trap_frame.h offsets are stale");
_Static_assert(offsetof(struct trap_frame, spsr) == TRAP_FRAME_OFF_SPSR, "trap_frame.h offsets are stale");
_Static_assert(offsetof(struct trap_frame, esr) == TRAP_FRAME_OFF_ESR, "trap_frame.h offsets are stale");
_Static_assert(offsetof(struct trap_frame, far) == TRAP_FRAME_OFF_FAR, "trap_frame.h offsets are stale");
_Static_assert(offsetof(struct trap_frame, vector_type) == TRAP_FRAME_OFF_VECTOR_TYPE, "trap_frame.h offsets are stale");

// The stub reserves TRAP_FRAME_SIZE, which must be sizeof rounded up to the 16 bytes
_Static_assert(TRAP_FRAME_SIZE == (sizeof(struct trap_frame) + 15) / 16 * 16, "trap_frame.h frame size is stale");

#endif  // __ASSEMBLER__

#endif  // PROS_AARCH64_TRAP_FRAME_H
