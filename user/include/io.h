#ifndef PROS_USER_IO_H
#define PROS_USER_IO_H

#include "syscall.h"

// lot of duplicated stuff,
// on purpose: user/ never includes kernel/include/
// someday a libc will provide them (i guess linux_dirent64 would be part of it ?)

#define STDIN 0
#define STDOUT 1
#define STDERR 2

#define O_RDONLY 0x0000

// only for literals, otherwise sizeof is wrong
#define PUTS_OUT(s) sys_write(STDOUT, (s), sizeof((s)) - 1)
#define PUTS_ERR(s) sys_write(STDERR, (s), sizeof((s)) - 1)
#define BUF_EQ_S(buf, s) (strncmp((buf), (s), sizeof((s))) == 0)

#define DT_REG 8
#define DT_DIR 4
#define DT_CHR 2
#define DT_UNKNOWN 0

struct linux_dirent64 {
    unsigned long d_ino;
    long d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

#endif  // PROS_USER_IO_H
