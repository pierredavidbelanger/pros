/* 
 * RISC-V 32-bit (RV32) Assembly Bootstrapper
 */

.section .text._start
.global _start

_start:
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
