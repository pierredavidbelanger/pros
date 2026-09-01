#include "drivers/console_dev.h"

#include "core/console.h"
#include "core/kprintf.h"
#include "core/ldisc.h"
#include "core/spinlock.h"
#include "errno.h"
#include "fs/vfs/vfs.h"
#include "mm/heap.h"
#include "proc/sched.h"
#include "stdc.h"

struct console_dev_node_data {
    struct ldisc ld;
    struct spinlock lock;
};

int64_t console_dev_ops_read(struct vfs_node *node, uint64_t offset, uint64_t size, void *buffer);
int64_t console_dev_ops_write(struct vfs_node *node, uint64_t offset, uint64_t size, const void *buffer);

static struct vfs_ops console_dev_ops = {
    .read = console_dev_ops_read,
    .write = console_dev_ops_write,
};

// we have ONE console
static struct console_dev_node_data *the_console;

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

    the_console = data;

    return node;
}

void console_dev_feed(uint8_t byte) {
    struct console_dev_node_data *data = the_console;
    if (!data) return;

    // same as xv6's ctrl-p,
    // we want to look at the task dump for debugging
    if (byte == 0x10) {
        sched_task_dump_all();
        return;
    }

    spinlock_lock(&data->lock);
    ldisc_feed(&data->ld, byte);
    bool ready = ldisc_ready(&data->ld);
    spinlock_unlock(&data->lock);

    // outside the lock, the sleeper will lock it back
    if (ready) wakeup(&data->ld);
}

int64_t console_dev_ops_read(struct vfs_node *node, uint64_t offset, uint64_t size, void *buffer) {
    if (!node || !buffer) return -EINVAL;
    (void)offset;  // a char stream has no position
    if (size == 0) return 0;

    struct console_dev_node_data *data = (struct console_dev_node_data *)node->priv_data;
    if (!data) return -EIO;

    struct ldisc *ld = &data->ld;
    if (!ld) return -EIO;

    uint64_t flags = spinlock_lock_irqsave(&data->lock);
    // while here because if we wake up, some other task may have consumed the line
    // so in that case we need to go back to sleep, otherwise the line is ours
    while (!ldisc_ready(ld)) sleep(ld, &data->lock);
    int64_t n = (int64_t)ldisc_drain(ld, buffer, size);
    spinlock_unlock_irqrestore(&data->lock, flags);

    return n;
}

int64_t console_dev_ops_write(struct vfs_node *node, uint64_t offset, uint64_t size, const void *buffer) {
    if (!node || !buffer) return -EINVAL;
    (void)offset;
    for (uint64_t i = 0; i < size; i++) {
        console_putc((char)((const uint8_t *)buffer)[i]);
    }
    return size;
}
