#include "drivers/block/mbr.h"

#include "mm/heap.h"
#include "core/memory.h"

#include <printf.h>

int mbr_parse(blockdev_t *dev) {
    if (!dev) return -1;

    // alloc for 1 block_size
    char *buffer = kmalloc(dev->block_size);

    // read 1 block
    if (blockdev_read(dev, 0, 1, buffer)) {
        kfree(buffer);
        return -2; // cant read form device
    }

    struct mbr_sector *sector = (struct mbr_sector *) buffer;
    if (sector->signature != 0xAA55) {
        kfree(buffer);
        return -3; // not a valid MBR
    }

    for (int i = 0; i < 4; i++) {
        struct mbr_partition_entry *partition = &sector->partitions[i];
        // is it used ?
        if (partition->type != 0x0) {
            // we create a new device that represent this partition
            struct blockdev *part = kmalloc(sizeof(struct blockdev));
            snprintf(part->name, BLOCKDEV_MAX_NAME, "%sp%d", dev->name, i);
            part->block_size = dev->block_size;
            part->total_blocks = partition->sector_count;
            part->is_partition = true;
            part->lba_offset = partition->lba_start;
            part->priv_data = dev->priv_data;
            part->read_blocks = dev->read_blocks;
            part->write_blocks = dev->write_blocks;
            // register it
            blockdev_register(part);
        }
    }

    kfree(buffer);

    return 0;
}
