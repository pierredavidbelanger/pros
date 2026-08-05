#include "drivers/blockdev.h"
#include "core/kprintf.h"
#include "core/memory.h"

static blockdev_t *devices[BLOCKDEV_MAX_DEVICES];
static size_t device_count = 0;

void blockdev_init(void) {
    memset(devices, 0, sizeof(devices));
    device_count = 0;
    kprintf("[BLK  ] Initialized block device subsystem\n");
}

int blockdev_register(blockdev_t *dev) {
    if (!dev || !dev->name[0] || dev->block_size == 0) {
        kprintf("[BLK  ] Error: Invalid block device registration attempt\n");
        return -1;
    }

    if (device_count >= BLOCKDEV_MAX_DEVICES) {
        kprintf("[BLK  ] Error: Cannot register '%s', maximum block devices reached (%d)\n",
                dev->name, BLOCKDEV_MAX_DEVICES);
        return -1;
    }

    // Check for duplicate name
    for (size_t i = 0; i < BLOCKDEV_MAX_DEVICES; i++) {
        if (devices[i] && strcmp(devices[i]->name, dev->name) == 0) {
            kprintf("[BLK  ] Error: Device '%s' already registered\n", dev->name);
            return -1;
        }
    }

    // Find free slot
    for (size_t i = 0; i < BLOCKDEV_MAX_DEVICES; i++) {
        if (devices[i] == NULL) {
            devices[i] = dev;
            device_count++;
            kprintf("[BLK  ] Registered block device '%s' (block_size: %u B, total_blocks: %llu, cap: %llu MB)\n",
                    dev->name,
                    dev->block_size,
                    (unsigned long long)dev->total_blocks,
                    (unsigned long long)(dev->total_blocks * dev->block_size / (1024 * 1024)));
            return 0;
        }
    }

    return -1;
}

int blockdev_unregister(blockdev_t *dev) {
    if (!dev) return -1;

    for (size_t i = 0; i < BLOCKDEV_MAX_DEVICES; i++) {
        if (devices[i] == dev) {
            kprintf("[BLK  ] Unregistered block device '%s'\n", dev->name);
            devices[i] = NULL;
            if (device_count > 0) device_count--;
            return 0;
        }
    }

    return -1;
}

blockdev_t *blockdev_get_by_name(const char *name) {
    if (!name) return NULL;

    for (size_t i = 0; i < BLOCKDEV_MAX_DEVICES; i++) {
        if (devices[i] && strcmp(devices[i]->name, name) == 0) {
            return devices[i];
        }
    }

    return NULL;
}

int blockdev_read(blockdev_t *dev, uint64_t lba, uint32_t count, void *buf) {
    if (!dev || !buf || count == 0) return -1;
    if (!dev->read_blocks) return -1;

    if (lba + count > dev->total_blocks) {
        kprintf("[BLK  ] Error: Read out of bounds on '%s' (LBA %llu + count %u > total %llu)\n",
                dev->name, (unsigned long long)lba, count, (unsigned long long)dev->total_blocks);
        return -1;
    }

    return dev->read_blocks(dev, lba, count, buf);
}

int blockdev_write(blockdev_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    if (!dev || !buf || count == 0) return -1;
    if (!dev->write_blocks) return -1;

    if (lba + count > dev->total_blocks) {
        kprintf("[BLK  ] Error: Write out of bounds on '%s' (LBA %llu + count %u > total %llu)\n",
                dev->name, (unsigned long long)lba, count, (unsigned long long)dev->total_blocks);
        return -1;
    }

    return dev->write_blocks(dev, lba, count, buf);
}
