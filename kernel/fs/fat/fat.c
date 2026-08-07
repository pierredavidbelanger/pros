#include "fs/fat.h"

#include "core/kprintf.h"
#include "core/memory.h"
#include "mm/heap.h"

#include "stdc.h"

// TODO: a lot of duplicated code in there, i barely know what i am doing, but this will hold for a while

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
int64_t fat_vfs_ops_read(struct vfs_node *node, uint64_t offset, uint64_t size, void *buffer);
struct vfs_node *fat_vfs_ops_finddir(struct vfs_node *node, const char *name);
int fat_vfs_ops_readdir(struct vfs_node *node, uint32_t index, struct vfs_dirent *out);

struct vfs_ops fat_vfs_ops = {
    .close = fat_vfs_ops_close,
    .read = fat_vfs_ops_read,
    .finddir = fat_vfs_ops_finddir,
    .readdir = fat_vfs_ops_readdir,
};

static inline char to_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
}

static bool match_fat16_filename(const char fat16_name[11], const char* search_name) {
    // FAT16 is case-insensitive, we compare using uppercase
    char formatted[11];
    memset(formatted, ' ', 11); // Initialize with spaces
    int i = 0; // index for search_name
    int j = 0; // index for formatted
    // Format the base name (up to 8 characters)
    while (search_name[i] != '\0' && search_name[i] != '.') {
        if (j >= 8) {
            // base name is longer than 8 characters, its a no match
            return false;
        }
        formatted[j++] = to_upper((unsigned char)search_name[i++]);
    }
    // Format the extension (up to 3 char), if it exists
    if (search_name[i] == '.') {
        i++; // Skip the '.'
        j = 8; // Move to the extension offset in the FAT16 format
        while (search_name[i] != '\0') {
            if (j >= 11) {
                // ext is longer than 3 characters, its a no match
                return false;
            }
            formatted[j++] = to_upper((unsigned char)search_name[i++]);
        }
    }
    // Compare the generated 11-byte array to the actual fat16_name
    return memcmp(fat16_name, formatted, 11) == 0;
}

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
    if (!node) return -1;
    if (node->priv_data) {
        struct fat_node_data *node_data = node->priv_data;
        // free the entry, but leave bpb there, it will be free when root node is unmounted
        if (node_data->entry) {
            kfree(node_data->entry);
        }
        kfree(node_data);
    }
    kfree(node);
    return 0;
}

int64_t fat_vfs_ops_read(struct vfs_node *node, uint64_t offset, uint64_t size, void *buffer) {
    if (!node) return -1;

    // A real read function has to follow the FAT chain across multiple clusters and handle offset.
    // But here we only want to do a quick test and our test file has less bytes than a cluster,
    // so the file is guaranteed to sit inside that single target_sector block

    struct fat_node_data *node_data = node->priv_data;

    struct blockdev *dev = node_data->dev;
    struct fat_bpb *bpb = node_data->bpb;
    struct fat_dir_entry *entry = node_data->entry;

    uint32_t fat_start_sector = bpb->reserved_sector_count;
    uint32_t fat_sectors = bpb->fat_size_16 ? bpb->fat_size_16 : bpb->fat_size_32;
    uint32_t root_start_sector = fat_start_sector + (bpb->fat_count * fat_sectors);
    uint32_t root_dir_sectors = ((bpb->root_entry_count * 32) + (bpb->bytes_per_sector - 1)) / bpb->bytes_per_sector;
    // The data region starts exactly where the root directory ends
    uint32_t data_start_sector = root_start_sector + root_dir_sectors;

    uint32_t first_cluster = entry->fst_clus_lo;
    uint32_t target_sector = data_start_sector + ((first_cluster - 2) * bpb->sectors_per_cluster);

    uint32_t cluster_size = bpb->sectors_per_cluster * bpb->bytes_per_sector;
    void *bounce_buffer = kmalloc(cluster_size);
    if (!bounce_buffer) return -1;

    if (blockdev_read(dev, target_sector, bpb->sectors_per_cluster, bounce_buffer) != 0) {
        kfree(bounce_buffer);
        return -1;
    }

    uint64_t bytes_to_copy = size;
    if (bytes_to_copy > entry->file_size) {
        bytes_to_copy = entry->file_size;
    }

    memcpy(buffer, bounce_buffer, bytes_to_copy);

    kfree(bounce_buffer);

    return bytes_to_copy;
}

struct vfs_node *fat_vfs_ops_finddir(struct vfs_node *node, const char *name) {
    if (!node || !name) return NULL;

    kprintf("[FAT  ] fat_vfs_ops_finddir name=%s\n", name);

    struct fat_node_data *node_data = node->priv_data;

    struct blockdev *dev = node_data->dev;
    struct fat_bpb *bpb = node_data->bpb;

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
        for (int j = 1; j < 11; j++) {
            found_entry_clean_name[char_idx++] = entry.name[j];
        }
        found_entry_clean_name[11] = '\0';
        if (match_fat16_filename(found_entry_clean_name, name)) {
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

    strncpy(sub_node->name, found_entry_clean_name, 11);
    sub_node->ops = &fat_vfs_ops;
    sub_node->priv_data = sub_node_data;

    return sub_node;
}

int fat_vfs_ops_readdir(struct vfs_node *node, uint32_t index, struct vfs_dirent *out) {
    if (!node) return -1;

    kprintf("[FAT  ] fat_vfs_ops_readdir as index=%d\n", index);

    struct fat_node_data *node_data = node->priv_data;

    struct blockdev *dev = node_data->dev;
    struct fat_bpb *bpb = node_data->bpb;

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
        return -1;
    }

    // read root_dir_sectors from root_start_sector into the buffer
    if (blockdev_read(dev, root_start_sector, root_dir_sectors, root_entries_buf) != 0) {
        kprintf("[FAT  ] failed to read root sectors\n");
        kfree(root_entries_buf);
        return -1;
    }

    struct fat_dir_entry *root_entries = root_entries_buf;

    uint32_t current_valid_index = 0;

    for (int i = 0; i < (root_dir_sectors * 16); i++) {
        struct fat_dir_entry entry = root_entries[i];
        // no more files on disk
        if ((uint8_t)entry.name[0] == DIR_ENTRY_FREE) {
            kfree(root_entries_buf);
            return 0; // EOF
        }
        // Skip invalid entries (Deleted, LFNs, Volume IDs)
        if ((uint8_t)entry.name[0] == DIR_ENTRY_DELETED) continue;
        if ((uint8_t)entry.attr == ATTR_LONG_NAME) continue;
        // don't list the Volume name as a file
        if ((uint8_t)entry.attr & ATTR_VOLUME_ID) continue;
        // Is this the index the user asked for?
        if (current_valid_index == index) {
            // FOUND IT
            char found_entry_clean_name[12];
            int char_idx = 0;
            uint8_t first_char = (entry.name[0] == DIR_ENTRY_KANJI) ? 0xE5 : entry.name[0];
            found_entry_clean_name[char_idx++] = (char)first_char;
            for (int j = 1; j < 11; j++) {
                found_entry_clean_name[char_idx++] = entry.name[j];
            }
            found_entry_clean_name[11] = '\0';
            strncpy(out->name, found_entry_clean_name, VFS_NAME_SIZE);
            // Set the flags so the user knows if it's a folder or file
            if (entry.attr & ATTR_DIRECTORY) {
                out->flags = VFS_DIRECTORY;
            } else {
                out->flags = VFS_FILE;
            }
            kfree(root_entries_buf);
            return 1; // Success
        }
        // but it wasn't the target index, keep counting
        current_valid_index++;
    }
    kfree(root_entries_buf);
    // if we get here, the index was out of bounds
    return 0;
}
