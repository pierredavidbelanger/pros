#ifndef PROS_SYSCALLS_H
#define PROS_SYSCALLS_H

#include "fs/vfs/vfs.h"
#include "stdc.h"

// standard open flags (O_RDONLY, O_WRONLY, O_RDWR, etc)
#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR 0x0002
#define O_CREAT 0x0040

// standard lseek whence
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define SIGCHLD 17

// impl in file.c
int64_t sys_openat(int dirfd, const char *path, int flags, int mode);
int64_t sys_open(const char *path, int flags);
int64_t sys_close(int fd);
int64_t sys_getdents64(int fd, void *dirp, uint64_t count);
int64_t sys_read(int fd, void *buf, uint64_t count);
int64_t sys_write(int fd, const void *buf, uint64_t count);
int64_t sys_lseek(int fd, int64_t offset, int whence);

// impl in task.c
int64_t sys_getpid(void);
int64_t sys_exit(int status);

// the implementation, Linux x86_64 argument order, aarch64 swaps the last two and we dont read them yet
int64_t sys_clone(uint64_t flags, uint64_t stack, uint64_t parent_tid, uint64_t child_tid, uint64_t tls);
int64_t sys_fork(void);  // sys_clone(SIGCHLD, 0, 0, 0, 0), like sys_open delegates to sys_openat
int64_t sys_wait4(int64_t pid, int *status, int options, void *rusage);  // status is a user pointer, write it through uaccess

#endif  // PROS_SYSCALLS_H
