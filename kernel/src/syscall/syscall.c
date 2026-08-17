#include "syscall/syscall.h"

#include "asm/unistd.h"
#include "core/console.h"
#include "core/syscalls.h"

#include "errno.h"

// special case for now
static int64_t sys_write_console_or_vfs(int fd, const void *buf, uint64_t count) {
    if (fd == 1 || fd == 2) {
        const char *chars = buf;
        for (uint64_t i = 0; i < count; i++) console_putc(chars[i]);
        return (int64_t) count;
    }
    return sys_write(fd, buf, count);
}

typedef int64_t (*syscall_handler_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

static syscall_handler_t syscall_table[NR_SYSCALLS] = {
    [SYS_write] = (syscall_handler_t) sys_write_console_or_vfs,
};

int64_t syscall_dispatch(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    if (num >= NR_SYSCALLS || !syscall_table[num]) return -ENOSYS;
    return syscall_table[num](a0, a1, a2, a3, a4, a5);
}
