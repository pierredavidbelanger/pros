.section .text

.macro ISR_NOERRCODE num
.global isr_stub_\num
isr_stub_\num:
    pushq $0         /* Dummy error code */
    pushq $\num      /* Interrupt number */
    jmp isr_common_stub
.endm

.macro ISR_ERRCODE num
.global isr_stub_\num
isr_stub_\num:
    pushq $\num      /* Interrupt number */
    jmp isr_common_stub
.endm

/* Exceptions without error code */
ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8  /* Double Fault */
ISR_NOERRCODE 9
ISR_ERRCODE   10 /* Invalid TSS */
ISR_ERRCODE   11 /* Segment Not Present */
ISR_ERRCODE   12 /* Stack Segment Fault */
ISR_ERRCODE   13 /* General Protection Fault */
ISR_ERRCODE   14 /* Page Fault */
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17 /* Alignment Check */
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_ERRCODE   30 /* Security Exception */
ISR_NOERRCODE 31
ISR_NOERRCODE 32
ISR_NOERRCODE 33
ISR_NOERRCODE 34
ISR_NOERRCODE 35
ISR_NOERRCODE 36
ISR_NOERRCODE 37
ISR_NOERRCODE 38
ISR_NOERRCODE 39
ISR_NOERRCODE 40
ISR_NOERRCODE 41
ISR_NOERRCODE 42
ISR_NOERRCODE 43
ISR_NOERRCODE 44
ISR_NOERRCODE 45
ISR_NOERRCODE 46
ISR_NOERRCODE 47

isr_common_stub:
    /* cs sits above int_no/error_code/rip. from ring 3 means GS is userland's and ours has to come in,
       same pairing syscall_entry has always had, and what lets the exit below decide from the frame alone */
    cmpq $0x3b, 24(%rsp)
    jne 1f
    swapgs
1:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rdi
    call x86_64_exception_handler
    movq %rax, %rsp /* restore from whatever frame the handler returned */

/* The one restore tail, three ways in: here, syscall_entry, and arch_trap_return for a task that never trapped.
   All of them arrive with rsp on a struct trap_frame, and after a switch that frame belongs to another task. */
.global x86_64_trap_return
x86_64_trap_return:
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    addq $16, %rsp /* Clean up pushed int_no and error_code */

    /* peek at cs without popping it, iretq needs it still on the stack.
       ring 3 gets its own GS back, ring 0 keeps ours */
    cmpq $0x3b, 8(%rsp)
    jne 1f
    swapgs
1:
    iretq

.global isr_stub_table
isr_stub_table:
    .quad isr_stub_0,  isr_stub_1,  isr_stub_2,  isr_stub_3
    .quad isr_stub_4,  isr_stub_5,  isr_stub_6,  isr_stub_7
    .quad isr_stub_8,  isr_stub_9,  isr_stub_10, isr_stub_11
    .quad isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15
    .quad isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19
    .quad isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23
    .quad isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27
    .quad isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31
    .quad isr_stub_32, isr_stub_33, isr_stub_34, isr_stub_35
    .quad isr_stub_36, isr_stub_37, isr_stub_38, isr_stub_39
    .quad isr_stub_40, isr_stub_41, isr_stub_42, isr_stub_43
    .quad isr_stub_44, isr_stub_45, isr_stub_46, isr_stub_47
