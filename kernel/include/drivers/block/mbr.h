#ifndef PROS_MBR_H
#define PROS_MBR_H

#include "drivers/block/blockdev.h"

#include "stdc.h"

// A single 16-byte partition entry
struct mbr_partition_entry {
    uint8_t  status;
    uint8_t  chs_first[3];
    uint8_t  type;            // 0x00 means empty/unused
    uint8_t  chs_last[3];
    uint32_t lba_start;       // The sector where the partition begins
    uint32_t sector_count;    // Total size of the partition
} __attribute__((packed));

// The full 512-byte MBR sector
struct mbr_sector {
    uint8_t bootstrap[446];
    struct mbr_partition_entry partitions[4];
    uint16_t signature;       // Must be 0xAA55
} __attribute__((packed));

int mbr_parse(blockdev_t *dev);

#endif //PROS_MBR_H
