#include "drivers/console_dev.h"

#include "core/console.h"
#include "core/console_input.h"
#include "core/kprintf.h"
#include "core/ldisc.h"
#include "errno.h"
#include "fs/vfs/vfs.h"
#include "mm/heap.h"
#include "stdc.h"

struct console_dev_node_data {
    struct ldisc ld;
};

int64_t console_dev_ops_read(struct vfs_node *node, uint64_t offset, uint64_t size, void *buffer);
int64_t console_dev_ops_write(struct vfs_node *node, uint64_t offset, uint64_t size, const void *buffer);

static struct vfs_ops console_dev_ops = {
    .read = console_dev_ops_read,
    .write = console_dev_ops_write,
};

struct vfs_node *console_dev_create(void) {
    struct vfs_node *node = kcalloc(1, sizeof(struct vfs_node));
    if (!node) return NULL;

    struct console_dev_node_data *data = kcalloc(1, sizeof(struct console_dev_node_data));
    if (!data) {
        kfree(node);
        return NULL;
    }
    ldisc_init(&data->ld, console_putc);

    snprintf(node->name, VFS_NAME_SIZE, "%s", "console");
    node->flags = VFS_CHARDEVICE;
    node->size = 0;
    node->ops = &console_dev_ops;
    node->priv_data = data;

    return node;
}

int64_t console_dev_ops_read(struct vfs_node *node, uint64_t offset, uint64_t size, void *buffer) {
    if (!node || !buffer) return -EINVAL;
    (void)offset;  // a char stream has no position
    if (size == 0) return 0;

    struct console_dev_node_data *data = (struct console_dev_node_data *)node->priv_data;
    if (!data) return -EIO;

    struct ldisc *ld = &data->ld;
    if (!ld) return -EIO;

    // we spin
    // interrupts stay on here, so the uart isr keeps filling the queue under us,
    // and a timer tick preempts this loop like it preempts BOOT's own idle loop
    while (!ldisc_ready(ld)) {
        uint8_t byte;
        if (console_input_pop(&byte)) ldisc_feed(ld, byte);
    }

    return (int64_t)ldisc_drain(ld, buffer, size);
}

int64_t console_dev_ops_write(struct vfs_node *node, uint64_t offset, uint64_t size, const void *buffer) {
    if (!node || !buffer) return -EINVAL;
    (void)offset;
    for (uint64_t i = 0; i < size; i++) {
        console_putc((char)((const uint8_t *)buffer)[i]);
    }
    return size;
}
