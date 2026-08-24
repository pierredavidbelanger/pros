#include "fs/vfs/vfs.h"

#include "core/kprintf.h"
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
    snprintf(mount->path, VFS_NAME_SIZE, "%s", path);

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

static struct vfs_node *vfs_inner_find_and_create(const char *path, bool stop_at_parent, uint32_t flags) {
    if (!path) return NULL;
    if (strlen(path) >= VFS_PATH_MAX) return NULL;

    struct vfs_node *target = vfs_get_mountpoint(&path);
    if (!target) return NULL;

    struct vfs_node *parent = target;

    char path_buf[VFS_PATH_MAX];
    snprintf(path_buf, VFS_PATH_MAX, "%s", path);

    char *saveptr;
    char *token = strtokr(path_buf, "/", &saveptr);
    while (token != NULL) {
        // no token after this one means this token is the last one
        char *next_token = strtokr(NULL, "/", &saveptr);
        bool is_last = next_token == NULL;

        if (is_last && stop_at_parent) {
            // this is the last token
            // are we told to stop there
            if ((target->flags & VFS_DIRECTORY) == 0) {
                // but this token is not a "parent"
                // caller dont expect we return a file here
                return NULL;
            }
            // that will return target which still point to parent
            break;
        }

        // nothing to find below a file
        if ((parent->flags & VFS_DIRECTORY) == 0) return NULL;
        // no ops available on parent
        if (!parent->ops) return NULL;
        if (!parent->ops->finddir) return NULL;
        // find the sub node of parent named token
        target = parent->ops->finddir(parent, token);

        if (target == NULL) {
            // target does not exists in parent
            // maybe we need to create something
            if (is_last && (flags & VFS_FILE) != 0) {
                // this is the last token
                // are we told to create file
                // we assume the caller knows what he is doing
                if (!parent->ops->create) return NULL;
                if (parent->ops->create(parent, token, VFS_FILE, &target) != 0) return NULL;
            } else if ((flags & VFS_DIRECTORY) != 0) {
                // this is the last token or not
                // are we told to create dir anyways
                // we assume the caller knows what he is doing
                if (!parent->ops->create) return NULL;
                if (parent->ops->create(parent, token, VFS_DIRECTORY, &target) != 0) return NULL;
            }
            if (target == NULL) {
                // target still does not exists
                // we were not told to create anything (or it failed)
                // this ends here, we will return a NULL target
                break;
            }
        }

        token = next_token;
        parent = target;
    }

    return target;
}

struct vfs_node *vfs_lookup(const char *path) {
    return vfs_inner_find_and_create(path, false, 0);
}

struct vfs_node *vfs_create(const char *path, uint32_t flags) {
    return vfs_inner_find_and_create(path, false, flags);
}

struct vfs_node *vfs_mkdir_parents(const char *path) {
    return vfs_inner_find_and_create(path, true, VFS_DIRECTORY);
}
