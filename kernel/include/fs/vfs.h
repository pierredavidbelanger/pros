#ifndef PROS_VFS_H
#define PROS_VFS_H

#include "stdc.h"

// Node flags to distinguish between file types
#define VFS_FILE        0x01
#define VFS_DIRECTORY   0x02
#define VFS_CHARDEVICE  0x04
#define VFS_BLOCKDEVICE 0x08

// Forward declaration so vfs_ops can reference vfs_node
struct vfs_node;
struct vfs_dirent; // We'll need this later for reading directories

struct vfs_ops {
    int (*open)(struct vfs_node *node);
    int (*close)(struct vfs_node *node);
    int64_t (*read)(struct vfs_node *node, uint64_t offset, uint64_t size, void *buffer);
    int64_t (*write)(struct vfs_node *node, uint64_t offset, uint64_t size, const void *buffer);
    
    // Directory specific operations
    struct vfs_node *(*finddir)(struct vfs_node *node, const char *name);
    int (*readdir)(struct vfs_node *node, uint32_t index, struct vfs_dirent *out);
};

struct vfs_node {
    char name[128];
    uint32_t flags;
    uint64_t inode;        // Unique ID for the file on disk
    uint64_t size;         // Size in bytes
    
    struct vfs_ops *ops;   // Pointer to the functions that implement this node
    void *priv_data;       // A driver-specific pointer (e.g., FAT cluster info)
};

struct vfs_mount {
    char path[128];
    struct vfs_node *root;
    struct vfs_mount *next;
};

void vfs_init(void);
int vfs_mount(const char *path, struct vfs_node *root_node);
struct vfs_node *vfs_get_mountpoint(const char **path);

#endif //PROS_VFS_H
