#include "fs/ramfs/ramfs.h"

#include "core/kprintf.h"
#include "core/memory.h"
#include "mm/heap.h"
#include "mm/pmm.h"

struct ramfs_node_data {
    struct vfs_node *first_child;
    struct vfs_node *next_sibling;
    // struct vfs_node *parent;
    void **pages; // virtual addrs (pmm_phys_to_virt of pmm_alloc(1))
    uint64_t page_count; // number of entries in `pages`, NOT a byte count
};

int ramfs_ops_create(struct vfs_node *dir, const char *name, uint32_t flags, struct vfs_node **out);
int64_t ramfs_ops_read(struct vfs_node *node, uint64_t offset, uint64_t size, void *buffer);
int64_t ramfs_ops_write(struct vfs_node *node, uint64_t offset, uint64_t size, const void *buffer);
struct vfs_node *ramfs_ops_finddir(struct vfs_node *node, const char *name);
int ramfs_ops_readdir(struct vfs_node *node, uint32_t index, struct vfs_dirent *out);

static struct vfs_ops ramfs_ops = {
    .create = ramfs_ops_create,
    .read = ramfs_ops_read,
    .write = ramfs_ops_write,
    .finddir = ramfs_ops_finddir,
    .readdir = ramfs_ops_readdir,
};

static struct vfs_node *ramfs_ops_inner_create(const char *name, uint32_t flags) {
    if (!name) return NULL;

    struct vfs_node *node = kcalloc(1, sizeof(struct vfs_node));
    if (!node) return NULL;

    snprintf(node->name, VFS_NAME_SIZE, "%s", name);
    node->flags = flags;
    node->size = 0;
    node->ops = &ramfs_ops;

    struct ramfs_node_data *node_data = kcalloc(1, sizeof(struct ramfs_node_data));
    if (!node_data) {
        kfree(node);
        return NULL;
    }

    node->priv_data = node_data;

    return node;
}

struct vfs_node *ramfs_create_root(void) {
    return ramfs_ops_inner_create("/", VFS_DIRECTORY);
}

int ramfs_ops_create(struct vfs_node *dir, const char *name, uint32_t flags, struct vfs_node **out) {
    if (!dir || !(dir->flags & VFS_DIRECTORY)) return -1;

    struct vfs_node *node = ramfs_ops_finddir(dir, name);
    if (node) return -1;

    node = ramfs_ops_inner_create(name, flags);
    if (!node) return -1;

    struct ramfs_node_data *dir_data = dir->priv_data;
    if (dir_data->first_child == NULL) {
        dir_data->first_child = node;
    } else {
        struct vfs_node *sibling = dir_data->first_child;
        struct ramfs_node_data *sibling_data = sibling->priv_data;
        while (sibling_data->next_sibling != NULL) {
            sibling = sibling_data->next_sibling;
            sibling_data = sibling->priv_data;
        }
        sibling_data->next_sibling = node;
    }

    if (out) {
        *out = node;
    }

    return 0;
}

int64_t ramfs_ops_read(struct vfs_node *node, uint64_t offset, uint64_t size, void *buffer) {
    if (!node) return -1;

    if (offset >= node->size) return 0;

    // size_t actual_size = size;
    // if (node->size < offset + size) {
        // actual_size = node->size - offset;
    // }

    // TODO: read from node_data.pages
    // struct ramfs_node_data *node_data = node->priv_data;

    return -1;
}

int64_t ramfs_ops_write(struct vfs_node *node, uint64_t offset, uint64_t size, const void *buffer) {
    if (!node) return -1;

    // TODO: write to node_data.pages
    // struct ramfs_node_data *node_data = node->priv_data;

    return 0;
}

struct vfs_node *ramfs_ops_finddir(struct vfs_node *node, const char *name) {
    if (!node) return NULL;

    struct ramfs_node_data *node_data = node->priv_data;

    struct vfs_node *child = node_data->first_child;
    while (child != NULL) {
        if (strncmp(child->name, name, VFS_NAME_SIZE) == 0) {
            return child;
        }
        struct ramfs_node_data *child_data = child->priv_data;
        child = child_data->next_sibling;
    }

    return NULL;
}

int ramfs_ops_readdir(struct vfs_node *node, uint32_t index, struct vfs_dirent *out) {
    if (!node || out == NULL) return -1;

    struct ramfs_node_data *node_data = node->priv_data;

    struct vfs_node *child = node_data->first_child;
    int i = 0;
    while (child != NULL) {
        if (i == index) {
            snprintf(out->name, VFS_NAME_SIZE, "%s", child->name);
            out->flags = child->flags;
            return 1;
        }
        struct ramfs_node_data *child_data = child->priv_data;
        child = child_data->next_sibling;
        i++;
    }

    return 0;
}
