#include "asm/unistd.h"

.global _start
_start:
    mov  x8, #SYS_write
    mov  x0, #1           // fd
    adr  x1, msg          // buf, msg lives right after this code
    mov  x2, #msg_len     // count
    svc  #0
    mrs  x0, sctlr_el1    // same fault Step 1 used
    b    .
msg:
    .ascii "hello from ring 3\n"
msg_len = . - msg
