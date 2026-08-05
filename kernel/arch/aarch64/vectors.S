.section .text

.macro VECTOR_ENTRY num
.balign 128
    /* allocate sizeof(aarch64_registers) */
    sub sp, sp, #304
    /* Save x0 in aarch64_registers now so we do not clobber it */
    stp x0,  x1,  [sp, #0]
    mov x0, #\num
    b vector_common_stub
.endm

.balign 2048
.global vector_table
vector_table:
    /* Current EL with SP_EL0 */
    VECTOR_ENTRY 0   /* Synchronous */
    VECTOR_ENTRY 1   /* IRQ */
    VECTOR_ENTRY 2   /* FIQ */
    VECTOR_ENTRY 3   /* SError */

    /* Current EL with SP_EL1 */
    VECTOR_ENTRY 4   /* Synchronous */
    VECTOR_ENTRY 5   /* IRQ */
    VECTOR_ENTRY 6   /* FIQ */
    VECTOR_ENTRY 7   /* SError */

    /* Lower EL using AArch64 */
    VECTOR_ENTRY 8   /* Synchronous */
    VECTOR_ENTRY 9   /* IRQ */
    VECTOR_ENTRY 10  /* FIQ */
    VECTOR_ENTRY 11  /* SError */

    /* Lower EL using AArch32 */
    VECTOR_ENTRY 12  /* Synchronous */
    VECTOR_ENTRY 13  /* IRQ */
    VECTOR_ENTRY 14  /* FIQ */
    VECTOR_ENTRY 15  /* SError */

vector_common_stub:
    /* Save the rest in aarch64_registers */
    stp x2,  x3,  [sp, #16]
    stp x4,  x5,  [sp, #32]
    stp x6,  x7,  [sp, #48]
    stp x8,  x9,  [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30,      [sp, #240]

    /* Save system registers */
    add x1, sp, #304  /* Original SP before trap */
    mrs x2, elr_el1
    mrs x3, spsr_el1
    mrs x4, esr_el1
    mrs x5, far_el1

    stp x1, x2, [sp, #248]  /* SP, ELR */
    stp x3, x4, [sp, #264]  /* SPSR, ESR */
    stp x5, x0, [sp, #280]  /* FAR, vector_type (from x0) */

    /* Pointer to struct aarch64_registers is SP */
    mov x0, sp
    bl aarch64_exception_handler

    /* Restore system registers */
    ldp x5, x0, [sp, #280]
    ldp x3, x4, [sp, #264]
    ldp x1, x2, [sp, #248]

    msr far_el1, x5
    msr esr_el1, x4
    msr spsr_el1, x3
    msr elr_el1, x2

    /* Restore x0..x30 */
    ldp x0,  x1,  [sp, #0]
    ldp x2,  x3,  [sp, #16]
    ldp x4,  x5,  [sp, #32]
    ldp x6,  x7,  [sp, #48]
    ldp x8,  x9,  [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30,      [sp, #240]

    add sp, sp, #304
    eret
