#include "drivers/console_dev.h"

#include "core/console.h"
#include "core/kprintf.h"
#include "fs/vfs/vfs.h"
#include "mm/heap.h"

#include "errno.h"

#include "stdc.h"

int64_t console_dev_ops_read(struct vfs_node *node, uint64_t offset, uint64_t size, void *buffer);
int64_t console_dev_ops_write(struct vfs_node *node, uint64_t offset, uint64_t size, const void *buffer);

static struct vfs_ops console_dev_ops = {
    .read = console_dev_ops_read,
    .write = console_dev_ops_write,
};

struct vfs_node *console_dev_create(void) {
    struct vfs_node *node = kcalloc(1, sizeof(struct vfs_node));
    if (!node) return NULL;

    snprintf(node->name, VFS_NAME_SIZE, "%s", "console");
    node->flags = VFS_CHARDEVICE;
    node->size = 0;
    node->ops = &console_dev_ops;

    return node;
}

int64_t console_dev_ops_read(struct vfs_node *node, uint64_t offset, uint64_t size, void *buffer) {
    if (!node || !buffer) return -EINVAL;
    return -ENOSYS;
}

int64_t console_dev_ops_write(struct vfs_node *node, uint64_t offset, uint64_t size, const void *buffer) {
    if (!node || !buffer) return -EINVAL;
    (void) offset;
    for (uint64_t i = 0; i < size; i++) {
        console_putc((char) ((const uint8_t *) buffer)[i]);
    }
    return size;
}
