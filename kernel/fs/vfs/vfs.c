#include "fs/vfs/vfs.h"

#include "core/memory.h"
#include "core/kprintf.h"
#include "mm/heap.h"

static struct vfs_mount *vfs_mount_list = NULL;

void vfs_init(void) {
    vfs_mount_list = NULL;
}

int vfs_mount(const char *path, struct vfs_node *root_node) {
    if (!path || !root_node) {
        return -1;
    }

    // Allocate memory for the new mount point
    struct vfs_mount *mount = kmalloc(sizeof(struct vfs_mount));
    if (!mount) {
        return -1;
    }

    // Copy the path string safely
    strncpy(mount->path, path, sizeof(mount->path) - 1);
    mount->path[sizeof(mount->path) - 1] = '\0';

    // Assign the root node and prepend to the global mount list
    mount->root = root_node;
    mount->next = vfs_mount_list;
    vfs_mount_list = mount;

    kprintf("[VFS  ] mounted %s at %s\n", root_node->name, path);

    return 0;
}

struct vfs_node *vfs_get_mountpoint(const char **path) {
    if (!path || !*path) {
        return NULL;
    }

    struct vfs_mount *best_match = NULL;
    size_t best_match_len = 0;

    // Iterate through all mounts to find the longest matching path prefix
    struct vfs_mount *current = vfs_mount_list;
    while (current != NULL) {
        size_t mount_len = strlen(current->path);

        // Check if the beginning of the path matches the mount path
        if (strncmp(*path, current->path, mount_len) == 0) {
            if (mount_len > best_match_len) {
                best_match = current;
                best_match_len = mount_len;
            }
        }
        current = current->next;
    }

    if (!best_match) {
        return NULL;
    }

    kprintf("[VFS  ] found mount point %s for %s\n", best_match->root->name, *path);

    // Update the path pointer to point *after* the mount point string.
    // E.g., if mount point is "/" and path is "/boot", *path will become "boot"
    *path = *path + best_match_len;

    // Advance past any lingering leading slashes in the remaining path
    while (**path == '/') {
        (*path)++;
    }

    kprintf("[VFS  ] path inside mount point %s is %s\n", best_match->root->name, *path);

    return best_match->root;
}
