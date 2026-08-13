#ifndef PROS_AARCH64_EXCEPTIONS_H
#define PROS_AARCH64_EXCEPTIONS_H

#include "trap_frame.h"

#include "stdc.h"

struct trap_frame {
    uint64_t x[31];       // x0..x30
    uint64_t sp;          // Original SP before trap
    uint64_t elr;         // ELR_EL1 (return address)
    uint64_t spsr;        // SPSR_EL1
    uint64_t esr;         // ESR_EL1 (exception syndrome)
    uint64_t far;         // FAR_EL1 (fault address)
    uint64_t vector_type; // Which vector slot (0..15)
};

// vectors.S read/write into this struct through the offsets in trap_frame.h.
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

struct trap_frame *aarch64_exception_handler(struct trap_frame *frame);

#endif //PROS_AARCH64_EXCEPTIONS_H
