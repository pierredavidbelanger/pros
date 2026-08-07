#include "fs/fat.h"

#include "core/kprintf.h"
#include "core/memory.h"
#include "mm/heap.h"

// WTH those guys were thinking when designing this!

void fat_init(struct blockdev *dev) {
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

    // iterate all the root entries
    struct fat_dir_entry *root_entries = root_entries_buf;
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
