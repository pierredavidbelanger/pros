#ifndef PROS_CONSOLE_DEV_H
#define PROS_CONSOLE_DEV_H

#include "fs/vfs/vfs.h"
#include "stdc.h"

struct vfs_node *console_dev_create(void);

void console_dev_feed(uint8_t byte);

#endif  // PROS_CONSOLE_DEV_H
