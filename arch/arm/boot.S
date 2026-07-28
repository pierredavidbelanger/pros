/* 
 * ARM 32-bit (ARMv7-A) Assembly Bootstrapper
 */

.section .text._start
.global _start

_start:
    /* Set up stack pointer for ARM */
    ldr sp, =_stack_top

    /* Pass Machine ID (r1) as 1st argument (r0) to kmain */
    mov r0, r1
    /* Pass DTB pointer (r2) as 2nd argument (r1) to kmain */
    mov r1, r2

    /* Jump to C kernel entry point */
    bl kmain

park:
    wfi
    b park
