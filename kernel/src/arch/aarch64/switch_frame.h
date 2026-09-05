#ifndef PROS_AARCH64_SWITCH_FRAME_H
#define PROS_AARCH64_SWITCH_FRAME_H

#define SWITCH_FRAME_OFF_X(n) (((n) - 19) * 8)  // x19 is the first callee-saved one
#define SWITCH_FRAME_SIZE 96                    // 12 regs, already a multiple of 16

#ifndef __ASSEMBLER__

#include "stdc.h"

// AAPCS64 callee-saved. no d8-d15: the kernel is built without fp/neon
struct switch_frame {
    uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
    uint64_t x29;  // fp
    uint64_t x30;  // lr, where ret goes: task_trampoline on a fresh task
};

_Static_assert(offsetof(struct switch_frame, x19) == SWITCH_FRAME_OFF_X(19), "switch_frame.h offsets are stale");
_Static_assert(offsetof(struct switch_frame, x29) == SWITCH_FRAME_OFF_X(29), "switch_frame.h offsets are stale");
_Static_assert(offsetof(struct switch_frame, x30) == SWITCH_FRAME_OFF_X(30), "switch_frame.h offsets are stale");
_Static_assert(sizeof(struct switch_frame) == SWITCH_FRAME_SIZE, "switch.S moves sp by SWITCH_FRAME_SIZE");

#endif  // __ASSEMBLER__

#endif  // PROS_AARCH64_SWITCH_FRAME_H
