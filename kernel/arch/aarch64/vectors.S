.section .bss
.balign 16
exception_stack_bottom:
    .skip 16384
exception_stack_top:

.section .text

.macro VECTOR_ENTRY num
.balign 128
    /* 1. Save original x0 and x1 in hardware scratch registers */
    msr tpidr_el1, x0
    msr tpidrro_el0, x1

    /* 2. Read FAR_EL1 and ESR_EL1 into x0 and x1 IMMEDIATELY at cycle 0 */
    mrs x0, far_el1
    mrs x1, esr_el1

    /* 3. Switch to dedicated exception stack in .bss */
    ldr x2, =exception_stack_top
    mov x3, sp          /* x3 = original SP before trap */
    mov sp, x2          /* sp = dedicated exception stack */
    sub sp, sp, #304

    /* 4. Store far_el1 (x0) and esr_el1 (x1) into frame */
    str x0, [sp, #280]  /* far */
    str x1, [sp, #272]  /* esr */
    str x3, [sp, #248]  /* original sp */

    /* 5. Restore original x0 and x1 from scratch registers */
    mrs x0, tpidr_el1
    mrs x1, tpidrro_el0

    /* 6. Save original x0, x1 into frame */
    stp x0, x1, [sp, #0]

    mov x0, #\num
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
    mrs x2, elr_el1
    mrs x3, spsr_el1

    str x2, [sp, #256] /* elr */
    str x3, [sp, #264] /* spsr */
    str x0, [sp, #288] /* vector_type */

    /* Pointer to struct aarch64_registers is SP */
    mov x0, sp
    bl aarch64_exception_handler

    /* Restore system registers */
    ldr x3, [sp, #264]     /* spsr_el1 */
    ldr x2, [sp, #256]     /* elr_el1 */

    msr spsr_el1, x3
    msr elr_el1, x2

    /* Save original x0 and x1 into scratch registers for final restore */
    ldr x0, [sp, #0]
    ldr x1, [sp, #8]
    msr tpidr_el1, x0
    msr tpidrro_el0, x1

    /* Restore x2..x30 from exception frame */
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

    /* Restore original SP */
    ldr x1, [sp, #248]
    mov sp, x1

    /* Restore original x0 and x1 */
    mrs x0, tpidr_el1
    mrs x1, tpidrro_el0
    eret
