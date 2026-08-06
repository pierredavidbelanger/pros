#ifndef PROS_FILE_H
#define PROS_FILE_H

#include "fs/vfs.h"

#define MAX_OPEN_FILES 256

struct file {
    struct vfs_node *node;
    uint64_t offset;
    uint32_t flags;
    int ref_count;
};

void file_init(void);

#endif //PROS_FILE_H
