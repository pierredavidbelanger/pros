#include "fs/fat.h"

#include "core/kprintf.h"
#include "core/memory.h"
#include "mm/heap.h"

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

struct fat_node_data {
    struct blockdev *dev;
    struct fat_bpb *bpb;
    struct fat_dir_entry *entry;
};

int fat_vfs_ops_close(struct vfs_node *node);

struct vfs_node *fat_vfs_ops_finddir(struct vfs_node *node, const char *name);

int64_t fat_vfs_ops_read(struct vfs_node *node, uint64_t offset, uint64_t size, void *buffer);

struct vfs_ops fat_vfs_ops = {
    .close = fat_vfs_ops_close,
    .finddir = fat_vfs_ops_finddir,
    .read = fat_vfs_ops_read,
};


struct vfs_node *fat_mount(struct blockdev *dev) {
    if (!dev) return NULL;

    // allocate a block_size buffer for the sector
    void *bpb_buf = kmalloc(dev->block_size);
    if (!bpb_buf) {
        kprintf("[FAT  ] out of memory for bpb buffer\n");
        return NULL;
    }

    // read sector 0 into the buffer
    if (blockdev_read(dev, 0, 1, bpb_buf) != 0) {
        kprintf("[FAT  ] failed to read bpb sector 0\n");
        kfree(bpb_buf);
        return NULL;
    }

    struct fat_bpb *bpb = bpb_buf;
    kprintf("[FAT  ] Successfully read Boot Sector (Sector 0)!\n");
    char oem_name[9];
    for (int i = 0; i < 8; i++) {
        oem_name[i] = bpb->oem_name[i];
    }
    oem_name[8] = '\0';
    kprintf("[FAT  ] OEM Name: %s\n", oem_name);
    kprintf("[FAT  ] Bytes per Sector: %d\n", bpb->bytes_per_sector);
    kprintf("[FAT  ] Sectors per Cluster: %d\n", bpb->sectors_per_cluster);

    // allocate a new vfs_node
    struct vfs_node *node = kmalloc(sizeof(struct vfs_node));
    if (!node) {
        kprintf("[FAT  ] out of memory for a vfs_node\n");
        kfree(bpb_buf);
        return NULL;
    }

    // allocate a new fat_node_data to store our private data
    struct fat_node_data *node_data = kmalloc(sizeof(struct fat_node_data));
    if (!node_data) {
        kprintf("[FAT  ] out of memory for a fat_node_data\n");
        kfree(node);
        kfree(bpb_buf);
        return NULL;
    }

    node_data->dev = dev;
    node_data->bpb = bpb;
    node_data->entry = NULL;

    strncpy(node->name, dev->name, VFS_NAME_SIZE);
    node->ops = &fat_vfs_ops;
    node->priv_data = node_data;

    return node;
}

int fat_vfs_ops_close(struct vfs_node *node) {
    return -1;
}

struct vfs_node *fat_vfs_ops_finddir(struct vfs_node *node, const char *name) {
    if (!node || !name) return NULL;

    kprintf("[FAT  ] fat_vfs_ops_finddir( , %s)\n", name);

    struct fat_node_data *node_data = node->priv_data;

    struct blockdev *dev = node_data->dev;
    struct fat_bpb *bpb = node_data->bpb;
    // struct fat_dir_entry *entry = node_data.entry;

    uint32_t fat_start_sector = bpb->reserved_sector_count;
    uint32_t fat_sectors = bpb->fat_size_16 ? bpb->fat_size_16 : bpb->fat_size_32;
    uint32_t root_start_sector = fat_start_sector + (bpb->fat_count * fat_sectors);
    uint32_t root_dir_sectors = ((bpb->root_entry_count * 32) + (bpb->bytes_per_sector - 1)) / bpb->bytes_per_sector;
    kprintf("[FAT  ] Root start sector: %d\n", root_start_sector);
    kprintf("[FAT  ] Root directory sectors: %d\n", root_dir_sectors);

    // allocate a buffer large enough to read all the entries in the root
    void *root_entries_buf = kmalloc(root_dir_sectors * bpb->bytes_per_sector);
    if (!root_entries_buf) {
        kprintf("[FAT  ] out of memory for root entries buffer\n");
        return NULL;
    }

    // read root_dir_sectors from root_start_sector into the buffer
    if (blockdev_read(dev, root_start_sector, root_dir_sectors, root_entries_buf) != 0) {
        kprintf("[FAT  ] failed to read root sectors\n");
        kfree(root_entries_buf);
        return NULL;
    }

    struct fat_dir_entry *root_entries = root_entries_buf;

    struct fat_dir_entry *found_entry = NULL;
    char found_entry_clean_name[12];

    // iterate all the root entries
    for (int i = 0; i < (root_dir_sectors * 16); i++) {
        struct fat_dir_entry entry = root_entries[i];
        if ((uint8_t) entry.name[0] == DIR_ENTRY_FREE) break;
        if ((uint8_t) entry.name[0] == DIR_ENTRY_DELETED) continue;
        if ((uint8_t) entry.attr == ATTR_LONG_NAME) continue;
        int char_idx = 0;
        uint8_t first_char = (entry.name[0] == DIR_ENTRY_KANJI) ? 0xE5 : entry.name[0];
        found_entry_clean_name[char_idx++] = (char)first_char;
        for (int i = 1; i < 11; i++) {
            found_entry_clean_name[char_idx++] = entry.name[i];
        }
        found_entry_clean_name[11] = '\0';
        strntrim(found_entry_clean_name, ' ', 12);
        if (strncmp(found_entry_clean_name, name, 11) == 0) {
            found_entry = &entry;
            break;
        }
    }

    if (!found_entry) {
        kprintf("[FAT  ] %s not found\n", name);
        kfree(root_entries_buf);
        return NULL;
    }

    // allocate a new fat_dir_entry for our copy
    struct fat_dir_entry *found_entry_copy = kmalloc(sizeof(struct fat_dir_entry));
    if (!found_entry_copy) {
        kprintf("[FAT  ] out of memory for a fat_dir_entry\n");
        kfree(root_entries_buf);
        return NULL;
    }
    memcpy(found_entry_copy, found_entry, sizeof(struct fat_dir_entry));

    // we can release this now, we have our copy
    kfree(root_entries_buf);

    // allocate a new vfs_node
    struct vfs_node *sub_node = kmalloc(sizeof(struct vfs_node));
    if (!sub_node) {
        kprintf("[FAT  ] out of memory for a vfs_node\n");
        kfree(found_entry_copy);
        return NULL;
    }

    // allocate a new fat_node_data to store our private data
    struct fat_node_data *sub_node_data = kmalloc(sizeof(struct fat_node_data));
    if (!sub_node_data) {
        kprintf("[FAT  ] out of memory for a fat_node_data\n");
        kfree(found_entry_copy);
        kfree(sub_node);
        return NULL;
    }

    sub_node_data->dev = dev;
    sub_node_data->bpb = bpb;
    sub_node_data->entry = found_entry_copy;

    strncpy(node->name, found_entry_clean_name, 11);
    sub_node->ops = &fat_vfs_ops;
    sub_node->priv_data = sub_node_data;

    return sub_node;
}

int64_t fat_vfs_ops_read(struct vfs_node *node, uint64_t offset, uint64_t size, void *buffer) {
    kprintf("[FAT  ] fat_vfs_ops_read\n");
    return -1;
}

static void old_reference_fat_init(struct blockdev *dev) {
    if (!dev) return;

    // allocate a block_size buffer for the sector
    void *bpb_buf = kmalloc(dev->block_size);
    if (!bpb_buf) {
        kprintf("[FAT  ] out of memory for bpb buffer\n");
        return;
    }

    // read sector 0 into the buffer
    if (blockdev_read(dev, 0, 1, bpb_buf) != 0) {
        kprintf("[FAT  ] failed to read bpb sector 0\n");
        kfree(bpb_buf);
        return;
    }

    kprintf("[FAT  ] Successfully read Boot Sector (Sector 0)!\n");

    // cast the raw bytes into the packed struct
    struct fat_bpb *bpb = bpb_buf;
    kprintf("[FAT  ] Bytes per Sector: %d\n", bpb->bytes_per_sector);
    kprintf("[FAT  ] Sectors per Cluster: %d\n", bpb->sectors_per_cluster);

    // the OEM name is exactly 8 bytes on disk without null-terminator
    // copy it to a safe buffer to print it
    char oem_name[9];
    for (int i = 0; i < 8; i++) {
        oem_name[i] = bpb->oem_name[i];
    }
    oem_name[8] = '\0';
    kprintf("[FAT  ] OEM Name: %s\n", oem_name);

    // FAT tables
    uint32_t fat_start_sector = bpb->reserved_sector_count;
    uint32_t fat_sectors = bpb->fat_size_16 ? bpb->fat_size_16 : bpb->fat_size_32;
    kprintf("[FAT  ] FAT tables start sector: %d\n", fat_start_sector);
    kprintf("[FAT  ] FAT count: %d\n", bpb->fat_count);
    kprintf("[FAT  ] FAT sectors: %d\n", fat_sectors);

    // root directory sectors
    uint32_t root_start_sector = fat_start_sector + (bpb->fat_count * fat_sectors);
    uint32_t root_dir_sectors = ((bpb->root_entry_count * 32) + (bpb->bytes_per_sector - 1)) / bpb->bytes_per_sector;
    kprintf("[FAT  ] Root start sector: %d\n", root_start_sector);
    kprintf("[FAT  ] Root directory sectors: %d\n", root_dir_sectors);

    // where data region start
    uint32_t data_start_sector = root_start_sector + root_dir_sectors;
    kprintf("[FAT  ] Data start sector: %d\n", data_start_sector);

    // official way to detect if FAT16 or FAT32
    uint32_t total_sectors = bpb->total_sectors_16 ? bpb->total_sectors_16 : bpb->total_sectors_32;
    uint32_t data_sectors = total_sectors - data_start_sector;
    uint32_t total_clusters = data_sectors / bpb->sectors_per_cluster;
    if (total_clusters < 4085) {
        kprintf("[FAT  ] Type: FAT12\n");
    } else if (total_clusters < 65525) {
        kprintf("[FAT  ] Type: FAT16\n");
    } else {
        kprintf("[FAT  ] Type: FAT32\n");
    }

    // allocate a buffer for the FAT
    void *fat_buf = kmalloc(bpb->fat_count * fat_sectors * bpb->bytes_per_sector);
    if (!bpb_buf) {
        kprintf("[FAT  ] out of memory for fat buffer\n");
        kfree(bpb_buf);
        return;
    }

    // read bpb->fat_count * fat_sectors from fat_start_sector into the buffer
    if (blockdev_read(dev, fat_start_sector, bpb->fat_count * fat_sectors, fat_buf) != 0) {
        kprintf("[FAT  ] failed to read fat sectors\n");
        kfree(fat_buf);
        kfree(bpb_buf);
        return;
    }

    uint16_t *fat = fat_buf;

    // allocate a buffer large enough to read all the entries in the root
    void *root_entries_buf = kmalloc(root_dir_sectors * bpb->bytes_per_sector);
    if (!root_entries_buf) {
        kprintf("[FAT  ] out of memory for root entries buffer\n");
        kfree(fat_buf);
        kfree(bpb_buf);
        return;
    }

    // read root_dir_sectors from root_start_sector into the buffer
    if (blockdev_read(dev, root_start_sector, root_dir_sectors, root_entries_buf) != 0) {
        kprintf("[FAT  ] failed to read root sectors\n");
        kfree(fat_buf);
        kfree(root_entries_buf);
        kfree(bpb_buf);
        return;
    }

    struct fat_dir_entry *root_entries = root_entries_buf;

    // iterate all the root entries
    for (int i = 0; i < (root_dir_sectors * 16); i++) {
        struct fat_dir_entry entry = root_entries[i];

        // there are no more files in this directory
        if ((uint8_t) entry.name[0] == DIR_ENTRY_FREE) {
            break;
        }

        // this file was deleted, skip it!
        if ((uint8_t) entry.name[0] == DIR_ENTRY_DELETED) {
            continue;
        }

        // this is a Long File Name chunk, skip it for now
        if ((uint8_t) entry.attr == ATTR_LONG_NAME) {
            continue;
        }

        // Safely copy the 11-character name to a null-terminated buffer
        char clean_name[12];
        int char_idx = 0;
        uint8_t first_char = (entry.name[0] == DIR_ENTRY_KANJI) ? 0xE5 : entry.name[0];
        clean_name[char_idx++] = (char)first_char;
        for (int i = 1; i < 11; i++) {
            clean_name[char_idx++] = (char)entry.name[i];
        }
        clean_name[11] = '\0';

        // Mask attributes using constants to know if we have ATTR_VOLUME_ID, ATTR_DIRECTORY a file (ATTR_ARCHIVE?)
        if ((uint8_t) entry.attr & ATTR_VOLUME_ID) {
            kprintf("[FAT  ] Found Volume Label: %s\n", clean_name);
        } else if ((uint8_t) entry.attr & ATTR_DIRECTORY) {
            kprintf("[FAT  ] Found Directory:    %s\n", clean_name);
        } else {
            kprintf("[FAT  ] Found File:         %s (%u bytes)\n", clean_name, entry.file_size);
            kprintf("[FAT  ]   First cluster: %u\n", entry.fst_clus_lo);
            kprintf("[FAT  ]   Next cluster: %u\n", fat[entry.fst_clus_lo]);
        }
    }

    // free the memory
    kfree(fat_buf);
    kfree(root_entries_buf);
    kfree(bpb_buf);
}
