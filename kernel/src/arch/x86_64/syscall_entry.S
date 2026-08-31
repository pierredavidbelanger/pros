.section .text

/*
 * struct trap_frame (idt.h), low-to-high memory offset:
 *   r15 r14 r13 r12 r11 r10 r9 r8 rbp rdi rsi rdx rcx rbx rax int_no error_code rip cs rflags rsp ss
 * isr.S's isr_common_stub builds this by pushing in exactly the reverse of that order (last push
 * ends up at the lowest address == offset 0 == r15). syscall has no hardware-pushed frame at all,
 * so this hand-builds the same layout from a standing start, same reverse order, so
 * sched_on_trap_exit()/task dumps/the exit path treat it identically either way.
 */

.global syscall_entry
syscall_entry:
    swapgs
    movq %rsp, %gs:8            /* x86_64_percpu0.user_rsp - stash the user stack */
    movq %gs:0, %rsp            /* x86_64_percpu0.kernel_rsp - switch to this task's kernel stack */

    pushq $0x33                 /* ss = X86_64_SELECTOR_USER_DATA */
    pushq %gs:8                 /* rsp (the stashed user rsp) */
    pushq %r11                  /* rflags, saved here by `syscall` itself */
    pushq $0x3b                 /* cs = X86_64_SELECTOR_USER_CODE */
    pushq %rcx                  /* rip, saved here by `syscall` itself */
    pushq $0                    /* error_code slot, unused for a syscall */
    pushq $0x80                 /* int_no slot: X86_64_SYSCALL_INT_NO_SENTINEL */

    /* GPRs, same order/slots as isr_common_stub */
    pushq %rax                  /* still holds the syscall number here */
    pushq %rbx
    pushq %rcx                  /* syscall already clobbered this with rip; same ABI cost as Linux's own r10 swap */
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11                  /* syscall already clobbered this with rflags */
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rdi
    call x86_64_syscall_handler
    movq %rax, %rsp             /* restore from whatever frame the handler returned */

    /* one tail for both stubs, in isr.S. it iretq's, never sysretq: we may not be going back to ring 3 */
    jmp x86_64_trap_return
