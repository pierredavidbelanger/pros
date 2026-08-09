#include "fs/tar/tar.h"

#include "core/kprintf.h"
#include "core/memory.h"
#include "mm/heap.h"
#include "fs/vfs/vfs.h"

// TAR Header Field Sizes
#define TAR_NAME_SIZE     100
#define TAR_MODE_SIZE       8
#define TAR_UID_SIZE        8
#define TAR_GID_SIZE        8
#define TAR_SIZE_SIZE      12
#define TAR_MTIME_SIZE     12
#define TAR_CHKSUM_SIZE     8
#define TAR_LINKNAME_SIZE 100
#define TAR_MAGIC_SIZE      6
#define TAR_VERSION_SIZE    2
#define TAR_UNAME_SIZE     32
#define TAR_GNAME_SIZE     32
#define TAR_DEVMAJOR_SIZE   8
#define TAR_DEVMINOR_SIZE   8
#define TAR_PREFIX_SIZE   155
#define TAR_PADDING_SIZE   12

// Typeflag
#define TAR_TYPE_REG      '0'   /* Regular file */
#define TAR_TYPE_REG_ALT '\0'   /* Alternate regular file marker */
#define TAR_TYPE_LNK      '1'   /* Hard link */
#define TAR_TYPE_SYM      '2'   /* Symbolic link */
#define TAR_TYPE_CHR      '3'   /* Character special device */
#define TAR_TYPE_BLK      '4'   /* Block special device */
#define TAR_TYPE_DIR      '5'   /* Directory */
#define TAR_TYPE_FIFO     '6'   /* FIFO special (named pipe) */
#define TAR_TYPE_CONT     '7'   /* Contiguous file */

// Magic and Version Strings
#define TMAGIC   "ustar"        /* POSIX ustar magic (5 bytes + null) */
#define TVERSION "00"           /* POSIX ustar version (2 bytes) */

// Total Block Definition
#define TAR_BLOCK_SIZE    512

/**
 * POSIX 1003.1-1988 (ustar) Standard TAR Header Layout.
 * Total size is exactly TAR_BLOCK_SIZE
 */
struct ustar_header {
    char name[TAR_NAME_SIZE];         /*   0: File name / Pathname */
    char mode[TAR_MODE_SIZE];         /* 100: Permissions (octal string) */
    char uid[TAR_UID_SIZE];           /* 108: User ID (octal string) */
    char gid[TAR_GID_SIZE];           /* 116: Group ID (octal string) */
    char size[TAR_SIZE_SIZE];         /* 124: File size in bytes (octal string) */
    char mtime[TAR_MTIME_SIZE];       /* 136: Modification time (octal epoch) */
    char chksum[TAR_CHKSUM_SIZE];     /* 148: Header checksum */
    char typeflag;                    /* 156: Type of entry */
    char linkname[TAR_LINKNAME_SIZE]; /* 157: Linked file name */
    char magic[TAR_MAGIC_SIZE];       /* 257: Magic string ("ustar\0") */
    char version[TAR_VERSION_SIZE];   /* 263: Version string ("00") */
    char uname[TAR_UNAME_SIZE];       /* 265: Owner user name */
    char gname[TAR_GNAME_SIZE];       /* 297: Owner group name */
    char devmajor[TAR_DEVMAJOR_SIZE]; /* 329: Device major number */
    char devminor[TAR_DEVMINOR_SIZE]; /* 337: Device minor number */
    char prefix[TAR_PREFIX_SIZE];     /* 345: Prefix for path extensions */
    char padding[TAR_PADDING_SIZE];   /* 500: Zero-padding to reach 512 bytes */
};

#define TAR_PATH_BUF_SIZE (TAR_NAME_SIZE + 1)

static uint64_t tar_parse_octal(const char *field, size_t field_len) {
    uint64_t value = 0;
    for (size_t i = 0; i < field_len && field[i] >= '0' && field[i] <= '7'; i++) {
        value = (value * 8) + (field[i] - '0');
    }
    return value;
}

int tar_load(uint64_t tar_addr, size_t tar_size) {
    if (tar_size < TAR_BLOCK_SIZE) return -1;

    uint64_t addr = tar_addr;
    while (addr < tar_addr + tar_size) {
        struct ustar_header *header = (struct ustar_header *) addr;
        if (header->name[0] == '\0') break;
        if (memcmp(header->magic, TMAGIC, TAR_MAGIC_SIZE)) return -1;
        if (memcmp(header->version, TVERSION, TAR_VERSION_SIZE)) return -1;

        uint64_t size = tar_parse_octal(header->size, TAR_SIZE_SIZE);

        // just support file and directory
        if (header->typeflag == TAR_TYPE_REG || header->typeflag == TAR_TYPE_REG_ALT || header->typeflag == TAR_TYPE_DIR) {

            char path[TAR_PATH_BUF_SIZE];
            snprintf(path, TAR_PATH_BUF_SIZE, "/%.*s", TAR_NAME_SIZE, header->name);

            if (!vfs_mkdir_parents(path)) return -1;

            if (header->typeflag == TAR_TYPE_DIR) {
                struct vfs_node *dir = vfs_create(path, VFS_DIRECTORY);
                if (!dir) return -1;
            } else {
                struct vfs_node *file = vfs_create(path, VFS_FILE);
                if (!file) return -1;
                // TODO write data
            }
        }

        addr += TAR_BLOCK_SIZE + (size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE * TAR_BLOCK_SIZE;
    }

    return 0;
}
