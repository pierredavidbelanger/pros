#ifndef PROS_TAR_H
#define PROS_TAR_H

#include "fs/vfs/vfs.h"

struct vfs_node *tar_mount(uint64_t tar_addr, size_t tar_size);

#endif //PROS_TAR_H
