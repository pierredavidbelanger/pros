#include "syscall/syscall.h"

#include "asm/unistd.h"
#include "core/syscalls.h"
#include "errno.h"

typedef int64_t (*syscall_handler_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

// one table, many signatures, C has no way to say that without a cast.
// safe on both our ABIs: args live in registers, a callee ignores what it never declared
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
static syscall_handler_t syscall_table[NR_SYSCALLS] = {
    [SYS_read] = (syscall_handler_t)sys_read,
    [SYS_write] = (syscall_handler_t)sys_write,
    [SYS_open] = (syscall_handler_t)sys_open,
    [SYS_openat] = (syscall_handler_t)sys_openat,
    [SYS_close] = (syscall_handler_t)sys_close,
    [SYS_getpid] = (syscall_handler_t)sys_getpid,
    [SYS_exit] = (syscall_handler_t)sys_exit,
    [SYS_getdents64] = (syscall_handler_t)sys_getdents64,
};
#pragma clang diagnostic pop

int64_t syscall_dispatch(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    if (num >= NR_SYSCALLS || !syscall_table[num]) return -ENOSYS;
    return syscall_table[num](a0, a1, a2, a3, a4, a5);
}
