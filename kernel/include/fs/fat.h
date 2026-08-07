#ifndef PROS_FAT_H
#define PROS_FAT_H

#include "drivers/block/blockdev.h"

#include "stdc.h"

// The BIOS Parameter Block (BPB) sits at byte 0 of sector 0 of a FAT partition.
// It describes the geometry of the disk and where the FAT tables are located.
// We use __attribute__((packed)) because the compiler must NOT insert any padding bytes;
// this struct must map 1:1 perfectly to the bytes on the disk!
struct fat_bpb {
    uint8_t  jmp[3];                 // 0x00: Jump instruction (usually EB XX 90)
    char     oem_name[8];            // 0x03: OEM Name in ASCII (e.g., "MSWIN4.1")
    uint16_t bytes_per_sector;       // 0x0B: Usually 512
    uint8_t  sectors_per_cluster;    // 0x0D: 1, 2, 4, 8, etc.
    uint16_t reserved_sector_count;  // 0x0E: Number of reserved sectors before the first FAT
    uint8_t  fat_count;              // 0x10: Number of FATs (almost always 2)
    uint16_t root_entry_count;       // 0x11: FAT12/16 only. 0 for FAT32.
    uint16_t total_sectors_16;       // 0x13: Total sectors (if 0, use total_sectors_32)
    uint8_t  media_type;             // 0x15: 0xF8 for hard disks
    uint16_t fat_size_16;            // 0x16: Sectors per FAT (FAT12/16 only. 0 for FAT32)
    uint16_t sectors_per_track;      // 0x18: Sectors per track
    uint16_t head_count;             // 0x1A: Number of heads
    uint32_t hidden_sectors;         // 0x1C: Hidden sectors
    uint32_t total_sectors_32;       // 0x20: Total sectors (if total_sectors_16 is 0)

    // FAT32 Extended Fields (Only valid if fat_size_16 == 0)
    uint32_t fat_size_32;            // 0x24: Sectors per FAT for FAT32
    uint16_t ext_flags;              // 0x28: Extended flags
    uint16_t fs_version;             // 0x2A: Filesystem version
    uint32_t root_cluster;           // 0x2C: First cluster of the root directory
    uint16_t fs_info;                // 0x30: FSINFO sector number
    uint16_t backup_boot_sector;     // 0x32: Backup boot sector
    uint8_t  reserved[12];           // 0x34: Reserved
    uint8_t  drive_number;           // 0x40: Drive number
    uint8_t  reserved1;              // 0x41: Reserved
    uint8_t  boot_signature;         // 0x42: Extended boot signature (0x29)
    uint32_t volume_id;              // 0x43: Volume serial number
    char     volume_label[11];       // 0x47: Volume label
    char     fs_type[8];             // 0x52: "FAT32   "
} __attribute__((packed));

// Attribute Flags (Offset 0x0B)
#define ATTR_READ_ONLY  0x01
#define ATTR_HIDDEN     0x02
#define ATTR_SYSTEM     0x04
#define ATTR_VOLUME_ID  0x08
#define ATTR_DIRECTORY  0x10
#define ATTR_ARCHIVE    0x20
#define ATTR_LONG_NAME  (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)

// Special First Characters in Name (Offset 0x00)
#define DIR_ENTRY_FREE      0x00  // Entry is free and all subsequent entries are free
#define DIR_ENTRY_DELETED   0xE5  // Entry is deleted, but can be reused
#define DIR_ENTRY_KANJI     0x05  // Replaces character 0x05 with 0xE5 (Kanji support)

struct fat_dir_entry {
    char name[11];            // 0x00: 8.3 format (8 char name, 3 char extension, no dot)
    uint8_t attr;             // 0x0B: Attributes (0x10 = Directory, 0x0F = Long File Name)
    uint8_t nt_res;
    uint8_t crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t fst_clus_hi;     // High 16 bits of cluster (0 on FAT16)
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t fst_clus_lo;     // Low 16 bits of cluster
    uint32_t file_size;       // Size in bytes
} __attribute__((packed));

void fat_init(struct blockdev *dev);

#endif //PROS_FAT_H
