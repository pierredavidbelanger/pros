#ifndef BLOCKDEV_H
#define BLOCKDEV_H

#include "stdc.h"

#define BLOCKDEV_MAX_NAME 32
#define BLOCKDEV_MAX_DEVICES 16

struct blockdev;

typedef struct blockdev {
    char name[BLOCKDEV_MAX_NAME];
    uint32_t block_size;     // Sector/block size in bytes (typically 512)
    uint64_t total_blocks;   // Total block/sector count
    bool is_partition; // is this device a partition
    uint64_t lba_offset; // where the partition actually starts
    void *priv_data;         // Private driver context pointer

    // Sector Read callback (returns 0 on success, negative error code on failure)
    int (*read_blocks)(struct blockdev *dev, uint64_t lba, uint32_t count, void *buf);

    // Sector Write callback (returns 0 on success, negative error code on failure)
    int (*write_blocks)(struct blockdev *dev, uint64_t lba, uint32_t count, const void *buf);
} blockdev_t;

// Initialize block device subsystem
void blockdev_init(void);

// Register a block device into the global device table
int blockdev_register(blockdev_t *dev);

// Unregister a block device by pointer
int blockdev_unregister(blockdev_t *dev);

// Lookup registered block device by name (e.g., "hd0")
blockdev_t *blockdev_get_by_name(const char *name);

// Sector Read helper with bounds checking
int blockdev_read(blockdev_t *dev, uint64_t lba, uint32_t count, void *buf);

// Sector Write helper with bounds checking
int blockdev_write(blockdev_t *dev, uint64_t lba, uint32_t count, const void *buf);

#endif // BLOCKDEV_H
