#ifndef PROS_USER_DIRENT_H
#define PROS_USER_DIRENT_H

// duplicated linux_dirent64,
// on purpose: user/ never includes kernel/include/
// someday a libc will provide them (i guess linux_dirent64 would be part of it ?)

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

#endif  // PROS_USER_DIRENT_H
