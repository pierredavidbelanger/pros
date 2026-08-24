#ifndef PROS_UNISTD_H
#define PROS_UNISTD_H

#ifdef __aarch64__

#define SYS_read 63
#define SYS_write 64
#define SYS_open 511  // for some reason sys_open does not exists on aarch64 in Linux, we will delegates to sys_openat
#define SYS_openat 56
#define SYS_close 57
#define SYS_getpid 172
#define SYS_exit 93
#define SYS_getdents64 61

#else

#define SYS_read 0
#define SYS_write 1
#define SYS_open 2
#define SYS_openat 257
#define SYS_close 3
#define SYS_getpid 39
#define SYS_exit 60
#define SYS_getdents64 217

#endif

#define NR_SYSCALLS 512

#endif  // PROS_UNISTD_H
