#ifndef PROS_X86_64_SWITCH_FRAME_H
#define PROS_X86_64_SWITCH_FRAME_H

#include "stdc.h"

// arch_task_switch_to push the SysV callee-saved set, nothing else
struct switch_frame {
    uint64_t r15, r14, r13, r12, rbp, rbx;
    uint64_t ret;  // what the final ret pops: task_trampoline on a fresh task
    uint64_t pad;  // fabricated frames only. a real frame stops at ret
};

_Static_assert(offsetof(struct switch_frame, ret) == 48, "switch.S pops 6 regs before its ret");
_Static_assert(sizeof(struct switch_frame) == 64, "carving 64 keeps a fabricated frame 16-aligned");

#endif  // PROS_X86_64_SWITCH_FRAME_H
