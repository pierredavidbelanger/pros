#include "fs/vfs/vfs.h"

#include "core/memory.h"
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

    // Update the path pointer to point *after* the mount point string.
    // E.g., if mount point is "/" and path is "/boot", *path will become "boot"
    *path = *path + best_match_len;

    // Advance past any lingering leading slashes in the remaining path
    while (**path == '/') {
        (*path)++;
    }

    return best_match->root;
}

struct vfs_node *vfs_lookup(const char *path) {
    if (!path) return NULL;

    struct vfs_node *target = vfs_get_mountpoint(&path);
    if (!target) return NULL;

    char subpath[VFS_NAME_SIZE];
    int subpath_index = 0;
    int path_index = 0;

    while (true) {
        char c = path[path_index];
        if ((c == '/' || c == '\0') && subpath_index > 0) {
            subpath[subpath_index] = '\0';
            if (!target->ops || !target->ops->finddir) return NULL;
            target = target->ops->finddir(target, subpath);
            if (!target) return NULL;
            subpath_index = 0;
            continue;
        }
        if (c != '/' && c != '\0') {
            subpath[subpath_index] = c;
            subpath_index++;
            if (subpath_index >= VFS_NAME_SIZE) return NULL;
        }
        if (c == '\0') break;
        path_index++;
    }

    return target;
}
