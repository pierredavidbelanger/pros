#include "fs/ramfs/ramfs.h"

#include "core/kprintf.h"
#include "core/memory.h"
#include "mm/heap.h"
#include "mm/pmm.h"

#include "errno.h"

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

static void *ramfs_walk_page(struct ramfs_node_data *node_data, uint64_t index, bool alloc) {
    if (!node_data) return NULL;

    // do we even have enough pages for the requested index or not
    if (index >= node_data->page_count) {
        // we dont alloc, this is the end
        if (!alloc) return NULL;

        // double the page count until we have enough for the requested index
        // node_data->page_count is initially set to 0, so we must have a floor here, 4 should be good
        uint64_t new_count = node_data->page_count ? node_data->page_count * 2 : 4;
        while (new_count <= index) new_count *= 2;

        // re-alloc and zero our pages given the new count
        void **new_pages = krealloc(node_data->pages, new_count * sizeof(void *));
        if (!new_pages) return NULL;
        memset(&new_pages[node_data->page_count], 0, (new_count - node_data->page_count) * sizeof(void *));

        // our new stuff
        node_data->pages = new_pages;
        node_data->page_count = new_count;
    }

    // do the requested page was already alloc or not
    if (node_data->pages[index] == NULL) {
        // we dont alloc, this is the end
        if (!alloc) return NULL;

        // get a fresh page and zero it
        uint64_t phys_addr = pmm_alloc(1);
        if (!phys_addr) return NULL;
        void *virt_addr = pmm_phys_to_virt(phys_addr);
        memset(virt_addr, 0, PAGE_SIZE);

        // our new stuff
        node_data->pages[index] = virt_addr;
    }

    return node_data->pages[index];
}

static struct vfs_node *ramfs_node_new(const char *name, uint32_t flags) {
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
    return ramfs_node_new("/", VFS_DIRECTORY);
}

int ramfs_ops_create(struct vfs_node *dir, const char *name, uint32_t flags, struct vfs_node **out) {
    if (!dir || !(dir->flags & VFS_DIRECTORY)) return -1;

    struct vfs_node *node = ramfs_ops_finddir(dir, name);
    if (node) return -1;

    node = ramfs_node_new(name, flags);
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

static uint64_t ramfs_get_chunk_info(uint64_t offset, const uint64_t remaining, uint64_t *page_index, uint64_t *page_offset) {
    // the page index and offset we need to transfer
    *page_index = offset / PAGE_SIZE;
    *page_offset = offset % PAGE_SIZE;
    // the chunk size we need to transfer
    uint64_t chunk_size = PAGE_SIZE - *page_offset;
    // the remaining we need to transfer is small, good this is the size we need
    if (chunk_size > remaining) chunk_size = remaining;
    return chunk_size;
}

int64_t ramfs_ops_read(struct vfs_node *node, uint64_t offset, uint64_t size, void *buffer) {
    if (!node || !(node->flags & VFS_FILE) || !buffer) return -EINVAL;

    // cant read pass the end
    if (offset >= node->size) return 0;

    // will not willingly read pass the end
    uint64_t max_size = node->size - offset;
    if (size > max_size) {
        size = max_size;
    }

    struct ramfs_node_data *node_data = node->priv_data;

    // at the start remaining is the requested size
    uint64_t remaining = size;
    while (remaining > 0) {

        // the page index and offset and chunk size
        uint64_t page_index = 0;
        uint64_t page_offset = 0;
        uint64_t chunk_size = ramfs_get_chunk_info(offset, remaining, &page_index, &page_offset);

        // get a page (without alloc, its read only)
        void *page_addr = ramfs_walk_page(node_data, page_index, false);
        if (page_addr) {
            // we got a page, transfer
            memcpy(buffer, (uint8_t *) page_addr + page_offset, chunk_size);
        } else {
            // we did not get a page
            // write zeros to the caller buffer
            memset(buffer, 0, chunk_size);
        }

        // move ahead the offset and the caller buffer pointer
        offset += chunk_size;
        buffer = (uint8_t *) buffer + chunk_size;

        // what we need to read in the next loop
        remaining -= chunk_size;
    }

    return size;
}

int64_t ramfs_ops_write(struct vfs_node *node, uint64_t offset, uint64_t size, const void *buffer) {
    if (!node || !(node->flags & VFS_FILE) || !buffer) return -EINVAL;

    struct ramfs_node_data *node_data = node->priv_data;

    // actual_size is 0 and will increment when we write
    size_t actual_size = 0;

    // at the start remaining is the requested size
    uint64_t remaining = size;
    while (remaining > 0) {

        // the page index and offset and chunk size
        uint64_t page_index = 0;
        uint64_t page_offset = 0;
        uint64_t chunk_size = ramfs_get_chunk_info(offset, remaining, &page_index, &page_offset);

        // get a page (with alloc, we want to write in it)
        void *page_addr = ramfs_walk_page(node_data, page_index, true);
        if (page_addr) {
            // we got a page, transfer
            memcpy((uint8_t *) page_addr + page_offset, buffer, chunk_size);
            actual_size += chunk_size;
        } else {
            // we did not get a page
            // break here, that will give the caller the actual_size
            break;
        }

        // move ahead the offset and the caller buffer pointer
        offset += chunk_size;
        buffer = (const uint8_t *) buffer + chunk_size;

        // what we need to read in the next loop
        remaining -= chunk_size;
    }

    // did we actually written something,
    // and if offset > previous size that means we changed the size
    if (actual_size > 0 && offset > node->size) {
        node->size = offset;
    }

    return actual_size;
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
    uint32_t i = 0;
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
