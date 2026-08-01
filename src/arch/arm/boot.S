/* 
 * ARM 32-bit (ARMv7-A) Assembly Bootstrapper
 */

.section .text._start
.global _start

_start:
    /* Set the address of our vector table into r0 */
    ldr r0, =vector_table
    /* This moves a core register (r0) to a coprocessor (p15,op0 is system control) register (c12,c0,op0 the VBAR) */
    mcr p15, 0, r0, c12, c0, 0

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

.global vector_table
.align 5
vector_table:
    b .                    /* 0x00: Reset (Infinite loop) */
    b .                    /* 0x04: Undefined Instruction */
    b svc_handler          /* 0x08: Software Interrupt (SVC / Syscall) */
    b .                    /* 0x0C: Prefetch Abort */
    b .                    /* 0x10: Data Abort */
    b .                    /* 0x14: Reserved */
    b .                    /* 0x18: IRQ Interrupt */
    b .                    /* 0x1C: FIQ Fast Interrupt */

.global svc_handler
svc_handler:
    /* Save r0-r12 and lr onto the stack (automatically subtracts from SP!) */
    stmfd sp!, {r0-r12, lr}

    /* Pass SP (pointer to saved registers) to C function in r0 */
    mov r0, sp
    bl trap_handler

    /* Restore r0-r12 and lr from stack */
    ldmfd sp!, {r0-r12, lr}

    /* Restore SPSR from r1 , not used for now */
    /* msr spsr_cxsf, r1 */

    /* Return from Exception: 'movs' restores CPSR status flags automatically */
    movs pc, lr
