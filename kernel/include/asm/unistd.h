#ifndef PROS_UNISTD_H
#define PROS_UNISTD_H

#ifdef __aarch64__

#define SYS_write 64

#else

#define SYS_write 1

#endif

#define NR_SYSCALLS 512

#endif //PROS_UNISTD_H
