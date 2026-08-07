#ifndef PROS_FAT_H
#define PROS_FAT_H

#include "drivers/block/blockdev.h"
#include "fs/vfs.h"

struct vfs_node *fat_mount(struct blockdev *dev);

#endif //PROS_FAT_H
