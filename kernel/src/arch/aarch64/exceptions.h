#ifndef PROS_AARCH64_EXCEPTIONS_H
#define PROS_AARCH64_EXCEPTIONS_H

#include "stdc.h"
#include "trap_frame.h"

// The DAIF masks, all clear in M_EL1H above.
// A frame with I set runs once and never gives the CPU back
#define AARCH64_SPSR_D (1ULL << 9)  // debug
#define AARCH64_SPSR_A (1ULL << 8)  // SError
#define AARCH64_SPSR_I (1ULL << 7)  // IRQ
#define AARCH64_SPSR_F (1ULL << 6)  // FIQ

// What eret restores as the processor state
#define AARCH64_SPSR_M_EL1H 0x5ULL  // M[3:0] = 0b0101, return to EL1 on SP_EL1
#define AARCH64_SPSR_M_EL0T 0x0ULL  // M[3:0] = 0b0000, return to EL0 on SP_EL0

// vectors.S reads this back out of the frame on the way out.
// Below 8 leaves SP_EL1 alone, 8 and above writes the frame sp into SP_EL0
#define AARCH64_VECTOR_CURRENT_EL_IRQ 5  // current EL on SP_EL1, IRQ
#define AARCH64_VECTOR_LOWER_EL_IRQ 9    // lower EL, so the exit path writes SP_EL0

struct trap_frame *aarch64_exception_handler(struct trap_frame *frame);

// the restore tail in vectors.S, entered with a frame to eret through
_Noreturn void aarch64_trap_return(struct trap_frame *frame);

#endif  // PROS_AARCH64_EXCEPTIONS_H
