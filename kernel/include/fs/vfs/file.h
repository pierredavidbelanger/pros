#ifndef PROS_FILE_H
#define PROS_FILE_H

#include "fs/vfs/vfs.h"

#define MAX_OPEN_FILES 256

struct file {
    struct vfs_node *node;
    uint64_t offset;
    uint32_t flags;
    int ref_count;
};

#define DT_REG 8
#define DT_DIR 4
#define DT_CHR 2
#define DT_UNKNOWN 0

struct linux_dirent64 {
    uint64_t d_ino;     // inode number
    int64_t d_off;      // opaque cookie: where to resume
    uint16_t d_reclen;  // total size of THIS entry, including padding
    uint8_t d_type;     // DT_REG, DT_DIR, DT_CHR, …
    char d_name[];      // NUL-terminated, then padding
};

struct file *file_ref(struct file *f);
void file_unref(struct file *f);

struct file *file_open_node(struct vfs_node *node, int flags);

#endif  // PROS_FILE_H
