.section .text

/*
 * void arch_task_switch_to(struct switch_frame **old, struct switch_frame *new)
 *
 * rdi is where to park the outgoing bookmark, rsi is the incoming one.
 * Only the SysV callee-saved set is saved here: the compiler already spilled whatever else it cared about.
 * The return address is the one our caller's `call` pushed, and it is the last field of the frame.
 */
.global arch_task_switch_to
arch_task_switch_to:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* the bookmark is the rsp we just built the frame at */
    movq %rsp, (%rdi)

    /* from here on we stand on the incoming task's kernel stack */
    movq %rsi, %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx

    /* pops the incoming task's return address: task_trampoline if it has never run */
    ret

/*
 * void arch_trap_return(struct trap_frame *frame)
 *
 * The way back for a task that never trapped. The tail wants the frame in rsp, not in rdi.
 */
.global arch_trap_return
arch_trap_return:
    movq %rdi, %rsp
    jmp x86_64_trap_return
