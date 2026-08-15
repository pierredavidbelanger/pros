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

// What eret restores as the processor state
#define AARCH64_SPSR_M_EL1H 0x5ULL // M[3:0] = 0b0101, return to EL1 on SP_EL1

// The DAIF masks, all clear in M_EL1H above.
// A frame with I set runs once and never gives the CPU back
#define AARCH64_SPSR_D (1ULL << 9) // debug
#define AARCH64_SPSR_A (1ULL << 8) // SError
#define AARCH64_SPSR_I (1ULL << 7) // IRQ
#define AARCH64_SPSR_F (1ULL << 6) // FIQ

// vectors.S reads this back out of the frame on the way out.
// Below 8 leaves SP_EL1 alone, 8 and above writes the frame sp into SP_EL0
#define AARCH64_VECTOR_CURRENT_EL_IRQ 5 // current EL on SP_EL1, IRQ

struct trap_frame *aarch64_exception_handler(struct trap_frame *frame);

#endif //PROS_AARCH64_EXCEPTIONS_H
