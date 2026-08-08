#include "fs/tar/tar.h"

#include "core/kprintf.h"
#include "core/memory.h"
#include "mm/heap.h"

#include <printf.h>

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

#define TAR_NAME_SIZE_NULL_TERMINATED (TAR_NAME_SIZE+1)

struct tar_node_data {
    struct vfs_node *next_sibling;
    struct vfs_node *first_child;
    void *data;
};

int64_t tar_ops_read(struct vfs_node *node, uint64_t offset, uint64_t size, void *buffer);
struct vfs_node *tar_ops_finddir(struct vfs_node *node, const char *name);
int tar_ops_readdir(struct vfs_node *node, uint32_t index, struct vfs_dirent *out);

static struct vfs_ops tar_ops = {
    .read = tar_ops_read,
    .finddir = tar_ops_finddir,
    .readdir = tar_ops_readdir,
};

static uint64_t tar_parse_octal(const char *field, size_t field_len) {
    uint64_t value = 0;
    for (size_t i = 0; i < field_len && field[i] >= '0' && field[i] <= '7'; i++) {
        value = (value * 8) + (field[i] - '0');
    }
    return value;
}

struct vfs_node *tar_mount(uint64_t tar_addr, size_t tar_size) {
    if (tar_size < TAR_BLOCK_SIZE) return NULL;

    struct vfs_node *root = kcalloc(1, sizeof(struct vfs_node));
    if (!root) return NULL;
    snprintf(root->name, VFS_NAME_SIZE, "%s", "/");
    root->flags = VFS_DIRECTORY;
    root->size = 0;
    root->ops = &tar_ops;
    struct tar_node_data *root_data = kcalloc(1, sizeof(struct tar_node_data));
    if (!root_data) {
        kfree(root);
        return NULL;
    }
    root->priv_data = root_data;

    char name_buf[TAR_NAME_SIZE_NULL_TERMINATED];
    char *saveptr;

    uint64_t addr = tar_addr;
    while (addr < tar_addr + tar_size) {
        struct ustar_header *header = (struct ustar_header *) addr;
        if (header->name[0] == '\0') break;
        if (memcmp(header->magic, TMAGIC, TAR_MAGIC_SIZE)) return NULL;
        if (memcmp(header->version, TVERSION, TAR_VERSION_SIZE)) return NULL;

        uint64_t size = tar_parse_octal(header->size, TAR_SIZE_SIZE);

        // just support file and directory
        if (header->typeflag == TAR_TYPE_REG || header->typeflag == TAR_TYPE_REG_ALT || header->typeflag == TAR_TYPE_DIR) {

            memcpy(name_buf, header->name, TAR_NAME_SIZE);
            name_buf[TAR_NAME_SIZE] = '\0';

            struct vfs_node *parent = root;

            kprintf("[TAR  ] Parse %s\n", name_buf);
            char *token = strtokr(name_buf, "/", &saveptr);
            while (token != NULL) {
                kprintf("[TAR  ]   Token: %s\n", token);

                struct vfs_node *child = parent->ops->finddir(parent, token);
                if (!child) {

                    kprintf("[TAR  ]   Token: %s, child not found\n", token);

                    child = kcalloc(1, sizeof(struct vfs_node));
                    if (!child) break;
                    snprintf(child->name, VFS_NAME_SIZE, "%s", token);
                    if (header->typeflag == TAR_TYPE_DIR) {
                        child->flags = VFS_DIRECTORY;
                    } else {
                        child->flags = VFS_FILE;
                    }
                    child->size = size;
                    child->ops = &tar_ops;
                    struct tar_node_data *child_data = kcalloc(1, sizeof(struct tar_node_data));
                    if (!child_data) {
                        kfree(child);
                        break;
                    }
                    if (child->flags == VFS_FILE) {
                        child_data->data = (void *) addr + TAR_BLOCK_SIZE;
                    }
                    child->priv_data = child_data;

                    struct tar_node_data *parent_data = parent->priv_data;
                    if (parent_data->first_child == NULL) {
                        parent_data->first_child = child;
                    } else {
                        struct vfs_node *sibling = parent_data->first_child;
                        struct tar_node_data *sibling_data = sibling->priv_data;
                        while (sibling_data->next_sibling != NULL) {
                            sibling = sibling_data->next_sibling;
                            sibling_data = sibling->priv_data;
                        }
                        sibling_data->next_sibling = child;
                    }
                } else {

                    kprintf("[TAR  ]   Token: %s, child found\n", token);

                }

                token = strtokr(NULL, "/", &saveptr);
                parent = child;
            }
        }

        addr += TAR_BLOCK_SIZE + (size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE * TAR_BLOCK_SIZE;
    }

    kprintf("[TAR  ] Mounted TAR %s in memory\n", root->name);

    return root;
}

int64_t tar_ops_read(struct vfs_node *node, uint64_t offset, uint64_t size, void *buffer) {
    if (!node) return -1;

    if (offset >= node->size) return 0;

    size_t actual_size = size;
    if (node->size < offset + size) {
        actual_size = node->size - offset;
    }

    struct tar_node_data *node_data = node->priv_data;
    memcpy(buffer, node_data->data + offset, actual_size);

    return actual_size;
}

struct vfs_node *tar_ops_finddir(struct vfs_node *node, const char *name) {
    if (!node) return NULL;

    struct tar_node_data *node_data = node->priv_data;

    struct vfs_node *child = node_data->first_child;
    while (child != NULL) {
        if (strncmp(child->name, name, VFS_NAME_SIZE) == 0) {
            return child;
        }
        struct tar_node_data *child_data = child->priv_data;
        child = child_data->next_sibling;
    }

    return NULL;
}

int tar_ops_readdir(struct vfs_node *node, uint32_t index, struct vfs_dirent *out) {
    if (!node || out == NULL) return -1;

    struct tar_node_data *node_data = node->priv_data;

    struct vfs_node *child = node_data->first_child;
    int i = 0;
    while (child != NULL) {
        if (i == index) {
            strncpy(out->name, child->name, VFS_NAME_SIZE);
            out->flags = child->flags;
            return 1;
        }
        struct tar_node_data *child_data = child->priv_data;
        child = child_data->next_sibling;
        i++;
    }

    return 0;
}
