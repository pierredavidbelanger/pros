/* 
 * RISC-V 32-bit (RV32) Assembly Bootstrapper
 */

.section .text._start
.global _start
_start:
    /* Setup trap vector, for all Hart */
    la t0, trap_vector
    csrw mtvec, t0

    /* Read Machine Hart ID into t0 */
    csrr t0, mhartid

    /* Park secondary cores */
    bnez t0, park

    /* Set up kernel stack pointer */
    la sp, _stack_top

    /* Call C kernel entry point */
    call kmain

park:
    wfi
    j park

.global trap_vector
.align 4
trap_vector:
    /* Save original sp into the mscratch CSR */
    csrw mscratch, sp

    /* Allocate 144 bytes on stack (16-byte aligned) */
    addi sp, sp, -144

    /* Save general-purpose registers x1-x31 */
    sw x1,   0(sp)          /* ra */
    sw x5,  16(sp)          /* t0, before we use it for the sp */
    csrr t0, mscratch
    sw t0,   4(sp)          /* original sp */
    sw x3,   8(sp)          /* gp */
    sw x4,  12(sp)          /* tp */
    sw x6,  20(sp)          /* t1 */
    sw x7,  24(sp)          /* t2 */
    sw x8,  28(sp)          /* s0 / fp */
    sw x9,  32(sp)          /* s1 */
    sw x10, 36(sp)          /* a0 */
    sw x11, 40(sp)          /* a1 */
    sw x12, 44(sp)          /* a2 */
    sw x13, 48(sp)          /* a3 */
    sw x14, 52(sp)          /* a4 */
    sw x15, 56(sp)          /* a5 */
    sw x16, 60(sp)          /* a6 */
    sw x17, 64(sp)          /* a7 */
    sw x18, 68(sp)          /* s2 */
    sw x19, 72(sp)          /* s3 */
    sw x20, 76(sp)          /* s4 */
    sw x21, 80(sp)          /* s5 */
    sw x22, 84(sp)          /* s6 */
    sw x23, 88(sp)          /* s7 */
    sw x24, 92(sp)          /* s8 */
    sw x25, 96(sp)          /* s9 */
    sw x26, 100(sp)         /* s10 */
    sw x27, 104(sp)         /* s11 */
    sw x28, 108(sp)         /* t3 */
    sw x29, 112(sp)         /* t4 */
    sw x30, 116(sp)         /* t5 */
    sw x31, 120(sp)         /* t6 */

    /* Save CSRs */
    csrr t0, mepc
    csrr t1, mcause
    csrr t2, mstatus
    sw t0, 124(sp)
    sw t1, 128(sp)
    sw t2, 132(sp)

    /* Call C handler with pointer to stack frame */
    mv a0, sp
    call trap_handler

    /* Restore CSRs (mepc may have been updated in C) */
    lw t0, 124(sp)
    lw t2, 132(sp)
    csrw mepc, t0
    csrw mstatus, t2

    /* Restore general-purpose registers (skip sp/x2) */
    lw x1,   0(sp)
    /* lw x2,   4(sp) */
    lw x3,   8(sp)
    lw x4,  12(sp)
    lw x5,  16(sp)
    lw x6,  20(sp)
    lw x7,  24(sp)
    lw x8,  28(sp)
    lw x9,  32(sp)
    lw x10, 36(sp)
    lw x11, 40(sp)
    lw x12, 44(sp)
    lw x13, 48(sp)
    lw x14, 52(sp)
    lw x15, 56(sp)
    lw x16, 60(sp)
    lw x17, 64(sp)
    lw x18, 68(sp)
    lw x19, 72(sp)
    lw x20, 76(sp)
    lw x21, 80(sp)
    lw x22, 84(sp)
    lw x23, 88(sp)
    lw x24, 92(sp)
    lw x25, 96(sp)
    lw x26, 100(sp)
    lw x27, 104(sp)
    lw x28, 108(sp)
    lw x29, 112(sp)
    lw x30, 116(sp)
    lw x31, 120(sp)

    /* Free stack & return from trap */
    addi sp, sp, 144
    mret
