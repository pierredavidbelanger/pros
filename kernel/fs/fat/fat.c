#include "fs/fat.h"

#include "core/kprintf.h"
#include "mm/heap.h"

void fat_init(struct blockdev *dev) {
    if (!dev) return;

    // allocate a block_size buffer for the sector
    void *buffer = kmalloc(dev->block_size);
    if (!buffer) {
        kprintf("[FAT  ] out of memory for sector buffer\n");
        return;
    }

    // read sector 0 into the buffer
    if (blockdev_read(dev, 0, 1, buffer) != 0) {
        kprintf("[FAT  ] failed to read sector 0\n");
        kfree(buffer);
        return;
    }

    // cast the raw bytes into the packed struct
    struct fat_bpb *bpb = (struct fat_bpb *) buffer;

    // the OEM name is exactly 8 bytes on disk without null-terminator
    // copy it to a safe buffer to print it
    char oem_name[9];
    for (int i = 0; i < 8; i++) {
        oem_name[i] = bpb->oem_name[i];
    }
    oem_name[8] = '\0';

    kprintf("[FAT  ] Successfully read Boot Sector (Sector 0)!\n");
    kprintf("[FAT  ] OEM Name: %s\n", oem_name);
    kprintf("[FAT  ] Bytes per Sector: %d\n", bpb->bytes_per_sector);
    kprintf("[FAT  ] Sectors per Cluster: %d\n", bpb->sectors_per_cluster);

    // free the memory buffer
    kfree(buffer);
}
