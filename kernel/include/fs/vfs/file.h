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

struct file *file_ref(struct file *f);
void file_unref(struct file *f);

struct file *file_open_node(struct vfs_node *node, int flags);

#endif  // PROS_FILE_H
