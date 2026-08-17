#include "fs/vfs/file.h"

#include "core/syscalls.h"
#include "core/memory.h"
#include "core/kprintf.h"

#include "errno.h"

static struct file open_files[MAX_OPEN_FILES];

void file_init(void) {
    memset(open_files, 0, sizeof(open_files));
}

// Find an empty file descriptor slot
static int allocate_fd(void) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (open_files[i].ref_count == 0) {
            return i;
        }
    }
    return -EAGAIN;
}

int sys_open(const char *path, int flags) {
    if (!path) return -ENOENT;

    // vfs_lookup will get the mount point and search each path segment with target->ops->finddir
    struct vfs_node *target_node = vfs_lookup(path);
    if (!target_node) return -ENOENT;

    // If the node has an open callback, give the driver a chance to initialize it
    if (target_node->ops && target_node->ops->open) {
        if (target_node->ops->open(target_node) != 0) return -EAGAIN;
    }

    int fd = allocate_fd();
    if (fd < 0) {
        return fd;
    }

    open_files[fd].node = target_node;
    open_files[fd].offset = 0;
    open_files[fd].flags = flags;
    open_files[fd].ref_count = 1;

    return fd;
}

int sys_close(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) return -EBADF;

    struct file *f = &open_files[fd];
    if (f->ref_count == 0) return -EBADF; // Already closed

    f->ref_count--;
    if (f->ref_count == 0) {
        // If the node has a close callback, call it
        if (f->node && f->node->ops && f->node->ops->close) {
            f->node->ops->close(f->node);
        }
        f->node = NULL;
    }

    return 0;
}

int sys_readdir(int fd, struct vfs_dirent *out) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) return -EBADF;
    if (!out) return -EFAULT;

    struct file *f = &open_files[fd];
    if (f->ref_count == 0 || !f->node) return -EBADF;
    if (!f->node->ops || !f->node->ops->readdir) return -ENOSYS;
    // Use f->offset as the directory index
    int res = f->node->ops->readdir(f->node, f->offset, out);

    // If the driver successfully found a file at this index, increment the offset
    // this way, the next time sys_readdir is called, it fetches the NEXT file
    if (res == 1) {
        f->offset++;
    }

    return res;
}

int64_t sys_read(int fd, void *buf, uint64_t count) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) return -EBADF;
    if (!buf) return -EFAULT;

    struct file *f = &open_files[fd];
    if (f->ref_count == 0 || !f->node) return -EBADF;

    if (!f->node->ops || !f->node->ops->read) return -ENOSYS; // Filesystem doesn't support reading

    int64_t bytes_read = f->node->ops->read(f->node, f->offset, count, buf);
    if (bytes_read > 0) {
        f->offset += bytes_read;
    }

    return bytes_read;
}

int64_t sys_write(int fd, const void *buf, uint64_t count) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) return -EBADF;
    if (!buf) return -EFAULT;

    struct file *f = &open_files[fd];
    if (f->ref_count == 0 || !f->node) return -EBADF;

    // Very basic write protection check
    if ((f->flags & O_WRONLY) == 0 && (f->flags & O_RDWR) == 0) return -EACCES; // File not opened for writing

    if (!f->node->ops || !f->node->ops->write) return -ENOSYS; // Filesystem doesn't support writing

    int64_t bytes_written = f->node->ops->write(f->node, f->offset, count, buf);
    if (bytes_written > 0) {
        f->offset += bytes_written;
    }

    return bytes_written;
}

int64_t sys_lseek(int fd, int64_t offset, int whence) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) return -EBADF;

    struct file *f = &open_files[fd];
    if (f->ref_count == 0 || !f->node) return -EBADF;

    switch (whence) {
        case SEEK_SET:
            f->offset = offset;
            break;
        case SEEK_CUR:
            f->offset += offset;
            break;
        case SEEK_END:
            f->offset = f->node->size + offset;
            break;
        default:
            return -EINVAL;
    }

    return f->offset;
}
