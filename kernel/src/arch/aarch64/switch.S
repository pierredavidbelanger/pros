#include "switch_frame.h"

.section .text

/*
 * void arch_task_switch_to(struct switch_frame **old, struct switch_frame *new)
 *
 * x0 is where to park the outgoing bookmark, x1 is the incoming one.
 * Only the callee-saved set is saved here: the compiler already spilled whatever else it cared about.
 */
.global arch_task_switch_to
arch_task_switch_to:
    sub sp, sp, #SWITCH_FRAME_SIZE
    stp x19, x20, [sp, #SWITCH_FRAME_OFF_X(19)]
    stp x21, x22, [sp, #SWITCH_FRAME_OFF_X(21)]
    stp x23, x24, [sp, #SWITCH_FRAME_OFF_X(23)]
    stp x25, x26, [sp, #SWITCH_FRAME_OFF_X(25)]
    stp x27, x28, [sp, #SWITCH_FRAME_OFF_X(27)]
    stp x29, x30, [sp, #SWITCH_FRAME_OFF_X(29)]

    /* the bookmark is the sp we just built the frame at */
    mov x2, sp
    str x2, [x0]

    /* from here on we stand on the incoming task's kernel stack */
    mov sp, x1

    ldp x19, x20, [sp, #SWITCH_FRAME_OFF_X(19)]
    ldp x21, x22, [sp, #SWITCH_FRAME_OFF_X(21)]
    ldp x23, x24, [sp, #SWITCH_FRAME_OFF_X(23)]
    ldp x25, x26, [sp, #SWITCH_FRAME_OFF_X(25)]
    ldp x27, x28, [sp, #SWITCH_FRAME_OFF_X(27)]
    ldp x29, x30, [sp, #SWITCH_FRAME_OFF_X(29)]
    add sp, sp, #SWITCH_FRAME_SIZE

    /* x30 is theirs now: task_trampoline if they have never run */
    ret
