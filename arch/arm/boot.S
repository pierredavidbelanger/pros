/* 
 * ARM 32-bit (ARMv7-A) Assembly Bootstrapper
 */

.section .text._start
.global _start

_start:
    /* Set up stack pointer for ARM */
    ldr sp, =_stack_top

    /* Jump to C kernel entry point */
    bl kmain

park:
    wfi
    b park
