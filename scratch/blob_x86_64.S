.global _start
_start:
    mov  $1, %rax          # SYS_write
    mov  $1, %rdi          # fd
    lea  msg(%rip), %rsi   # buf
    mov  $msg_len, %rdx    # count
    syscall
    hlt
    jmp  .
msg:
    .ascii "hello from ring 3\n"
msg_len = . - msg
