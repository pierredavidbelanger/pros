#include "fs/vfs/file.h"

#include "core/syscalls.h"
#include "core/memory.h"
#include "core/kprintf.h"
#include "mm/heap.h"
#include "mm/uaccess.h"
#include "proc/sched.h"

#include "errno.h"

#define COPY_BUFFER_SIZE 512

#define AT_FDCWD -100 // from Linux

static struct file **current_task_fds() {
    struct task *task = sched_get_current_task();
    if (!task) return NULL;
    return task->fds;
}

int64_t sys_openat(int dirfd, const char *path, int flags, int mode) {
    if (dirfd != AT_FDCWD) return -ENOSYS; // do not support dirfd relative open for now
    if (!path) return -ENOENT;

    // vfs_lookup will get the mount point and search each path segment with target->ops->finddir
    struct vfs_node *target_node = vfs_lookup(path);
    if (!target_node) return -ENOENT;

    // If the node has an open callback, give the driver a chance to initialize it
    if (target_node->ops && target_node->ops->open) {
        if (target_node->ops->open(target_node) != 0) return -EAGAIN;
    }

    struct file **fds = current_task_fds();
    if (!fds) return -EBADF;

    // find an unused slot
    // after FSs 0-2: they are reserved fo stdin, stdout and stderr
    int fd = -1;
    for (int i = 3; i < MAX_OPEN_FILES; i++) {
        if (!fds[i]) {
            fd = i;
            break;
        }
    }
    if (fd < 0) {
        return -EAGAIN;
    }

    fds[fd] = kmalloc(sizeof(struct file));
    if (!fds[fd]) return -ENOMEM;

    fds[fd]->node = target_node;
    fds[fd]->offset = 0;
    fds[fd]->flags = flags;
    fds[fd]->ref_count = 1;

    return fd;
}

int64_t sys_open(const char *path, int flags) {
    return sys_openat(AT_FDCWD, path, flags, 0);
}

int64_t sys_close(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) return -EBADF;

    struct file **fds = current_task_fds();
    if (!fds) return -EBADF;

    struct file *f = fds[fd];
    if (!f) return -EBADF; // Already closed

    // last sys_close on a file will kfree it
    f->ref_count--;
    if (f->ref_count == 0) {
        // If the node has a close callback, call it
        if (f->node && f->node->ops && f->node->ops->close) {
            f->node->ops->close(f->node);
        }
        kfree(f);
    }

    fds[fd] = NULL;

    return 0;
}

int64_t sys_readdir(int fd, struct vfs_dirent *out) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) return -EBADF;
    if (!out) return -EFAULT;

    struct file **fds = current_task_fds();
    if (!fds) return -EBADF;

    struct file *f = fds[fd];
    if (!f) return -EBADF;
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

    struct file **fds = current_task_fds();
    if (!fds) return -EBADF;

    struct file *f = fds[fd];
    if (!f) return -EBADF;
    if (!f->node->ops || !f->node->ops->read) return -ENOSYS; // Filesystem doesn't support reading

    char chunk[COPY_BUFFER_SIZE];
    int64_t total_count = 0;
    while (total_count < count) {
        uint64_t chunk_len = (count - total_count) < COPY_BUFFER_SIZE ? (count - total_count) : COPY_BUFFER_SIZE;
        int64_t actual_chunk_len = f->node->ops->read(f->node, f->offset, chunk_len, chunk);
        if (actual_chunk_len > 0) {
            if (copy_to_user((uint8_t *) buf + total_count, chunk, actual_chunk_len) != 0) {
                // keep what already reached the caller
                return total_count > 0 ? total_count : -EFAULT;
            }
            total_count += actual_chunk_len;
            f->offset += actual_chunk_len;
        } else if (actual_chunk_len < 0 && total_count == 0) {
            // a real error and we read nothing yet
            return actual_chunk_len;
        } else {
            // EOF, or a later error after a partial read
            break;
        }
    }

    return total_count;
}

int64_t sys_write(int fd, const void *buf, uint64_t count) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) return -EBADF;
    if (!buf) return -EFAULT;

    struct file **fds = current_task_fds();
    if (!fds) return -EBADF;

    struct file *f = fds[fd];
    if (!f) return -EBADF;

    // Very basic write protection check
    if ((f->flags & O_WRONLY) == 0 && (f->flags & O_RDWR) == 0) return -EACCES; // File not opened for writing

    if (!f->node->ops || !f->node->ops->write) return -ENOSYS; // Filesystem doesn't support writing

    char chunk[COPY_BUFFER_SIZE];
    int64_t total_count = 0;
    while (total_count < count) {
        uint64_t chunk_len = (count - total_count) < COPY_BUFFER_SIZE ? (count - total_count) : COPY_BUFFER_SIZE;
        if (copy_from_user(chunk, (uint8_t *) buf + total_count, chunk_len) != 0) {
            // keep what already made it out
            return total_count > 0 ? total_count : -EFAULT;
        }
        int64_t actual_chunk_len = f->node->ops->write(f->node, f->offset, chunk_len, chunk);
        if (actual_chunk_len > 0) {
            total_count += actual_chunk_len;
            f->offset += actual_chunk_len;
        } else if (actual_chunk_len < 0 && total_count == 0) {
            // a real error and we wrote nothing yet
            return actual_chunk_len;
        } else {
            // a short write, or a later error after a partial write
            break;
        }
    }

    return total_count;
}

int64_t sys_lseek(int fd, int64_t offset, int whence) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) return -EBADF;

    struct file **fds = current_task_fds();
    if (!fds) return -EBADF;

    struct file *f = fds[fd];
    if (!f) return -EBADF;

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
