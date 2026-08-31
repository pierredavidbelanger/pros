#include "trap_frame.h"

.section .text

/*
 * The frame is built on the stack the trap arrived on.
 * The only thing that differs between vector groups is where the interrupted SP has to be read from.
 */
.macro VECTOR_ENTRY num
.balign 128
    stp x0, x1, [sp, #-TRAP_FRAME_SIZE]!   /* frame is TRAP_FRAME_SIZE bytes; sp now points at it */
    /* x0 and x1 are now free to use, x0 will have the slot number, and x1 the sp */
    mov x0, #\num
.if (\num >= 4) && (\num <= 7)
    /* Current EL, EL1h: the trap arrived on this same stack, so the interrupted sp is just where this frame starts plus how big it is. */
    add x1, sp, #TRAP_FRAME_SIZE
.elseif (\num >= 8) && (\num <= 11)
    /* Lower EL: the kernel's sp is SP_EL1 and was never disturbed. The interrupted one belongs to userland and lives in SP_EL0. */
    mrs x1, sp_el0
.else
    /* Slots 0-3 are Current EL on SP_EL0, unreachable since arch_init() selected SP_EL1,
       and 12-15 are AArch32, which this kernel never enters. */
    mov x1, xzr
.endif
    b vector_common_stub
.endm

.balign 2048
.global vector_table
vector_table:
    /* Current EL with SP_EL0 */
    VECTOR_ENTRY 0
    VECTOR_ENTRY 1
    VECTOR_ENTRY 2
    VECTOR_ENTRY 3
    /* Current EL with SP_EL1 */
    VECTOR_ENTRY 4
    VECTOR_ENTRY 5
    VECTOR_ENTRY 6
    VECTOR_ENTRY 7
    /* Lower EL using AArch64 */
    VECTOR_ENTRY 8
    VECTOR_ENTRY 9
    VECTOR_ENTRY 10
    VECTOR_ENTRY 11
    /* Lower EL using AArch32 */
    VECTOR_ENTRY 12
    VECTOR_ENTRY 13
    VECTOR_ENTRY 14
    VECTOR_ENTRY 15

vector_common_stub:
    stp x2,  x3,  [sp, #TRAP_FRAME_OFF_X(2)]
    stp x4,  x5,  [sp, #TRAP_FRAME_OFF_X(4)]
    stp x6,  x7,  [sp, #TRAP_FRAME_OFF_X(6)]
    stp x8,  x9,  [sp, #TRAP_FRAME_OFF_X(8)]
    stp x10, x11, [sp, #TRAP_FRAME_OFF_X(10)]
    stp x12, x13, [sp, #TRAP_FRAME_OFF_X(12)]
    stp x14, x15, [sp, #TRAP_FRAME_OFF_X(14)]
    stp x16, x17, [sp, #TRAP_FRAME_OFF_X(16)]
    stp x18, x19, [sp, #TRAP_FRAME_OFF_X(18)]
    stp x20, x21, [sp, #TRAP_FRAME_OFF_X(20)]
    stp x22, x23, [sp, #TRAP_FRAME_OFF_X(22)]
    stp x24, x25, [sp, #TRAP_FRAME_OFF_X(24)]
    stp x26, x27, [sp, #TRAP_FRAME_OFF_X(26)]
    stp x28, x29, [sp, #TRAP_FRAME_OFF_X(28)]
    str x30,      [sp, #TRAP_FRAME_OFF_X(30)]

    /* Interrupted sp, captured by VECTOR_ENTRY that knows which group it belongs to */
    str x1, [sp, #TRAP_FRAME_OFF_SP]

    /* Save system registers */
    mrs x2, elr_el1
    mrs x3, spsr_el1
    str x2, [sp, #TRAP_FRAME_OFF_ELR]
    str x3, [sp, #TRAP_FRAME_OFF_SPSR]
    mrs x2, esr_el1
    mrs x3, far_el1
    str x2, [sp, #TRAP_FRAME_OFF_ESR]
    str x3, [sp, #TRAP_FRAME_OFF_FAR]
    str x0, [sp, #TRAP_FRAME_OFF_VECTOR_TYPE]

    /* Pointer to struct trap_frame is SP */
    mov x0, sp
    bl aarch64_exception_handler

/* Also the way back for a task that never trapped: task_trampoline calls in here with x0 = its frame.
   Above the vector_type test on purpose, that decision belongs to the frame, not to how we got here. */
.global aarch64_trap_return
aarch64_trap_return:
    mov sp, x0 /* restore from whatever frame the handler returned */

    /* Restore system registers */
    ldr x3, [sp, #TRAP_FRAME_OFF_SPSR]
    ldr x2, [sp, #TRAP_FRAME_OFF_ELR]

    msr spsr_el1, x3
    msr elr_el1, x2

    ldr x2, [sp, #TRAP_FRAME_OFF_VECTOR_TYPE]
    /* Which stack pointer ->sp goes back to is a property of the frame being returned to, not of the trap that got us here.
       after a context switch those are two different tasks, possibly from different exception levels.
       VECTOR_ENTRY can decide this with .if because it is generated per slot; here it has to be read out of the frame. */
    cmp x2, #8
    /* < 8 : Current EL: nothing to do, the post-indexed ldp below already leaves sp (SP_EL1) at the top of this frame's stack */
    b.lo .Lrestore_gprs
    /* >= 8 : Lower EL: sp is userland stack pointer and belongs in SP_EL0 and must never reach SP_EL1 */
    ldr x2, [sp, #TRAP_FRAME_OFF_SP]
    msr sp_el0, x2

.Lrestore_gprs:
    /* Restore x2..x30 from trap_frame */
    ldp x2,  x3,  [sp, #TRAP_FRAME_OFF_X(2)]
    ldp x4,  x5,  [sp, #TRAP_FRAME_OFF_X(4)]
    ldp x6,  x7,  [sp, #TRAP_FRAME_OFF_X(6)]
    ldp x8,  x9,  [sp, #TRAP_FRAME_OFF_X(8)]
    ldp x10, x11, [sp, #TRAP_FRAME_OFF_X(10)]
    ldp x12, x13, [sp, #TRAP_FRAME_OFF_X(12)]
    ldp x14, x15, [sp, #TRAP_FRAME_OFF_X(14)]
    ldp x16, x17, [sp, #TRAP_FRAME_OFF_X(16)]
    ldp x18, x19, [sp, #TRAP_FRAME_OFF_X(18)]
    ldp x20, x21, [sp, #TRAP_FRAME_OFF_X(20)]
    ldp x22, x23, [sp, #TRAP_FRAME_OFF_X(22)]
    ldp x24, x25, [sp, #TRAP_FRAME_OFF_X(24)]
    ldp x26, x27, [sp, #TRAP_FRAME_OFF_X(26)]
    ldp x28, x29, [sp, #TRAP_FRAME_OFF_X(28)]
    ldr x30,      [sp, #TRAP_FRAME_OFF_X(30)]

    ldp x0, x1, [sp], #TRAP_FRAME_SIZE   /* restore x0/x1 and release the frame in one go */
    eret
