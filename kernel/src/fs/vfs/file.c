#include "fs/vfs/file.h"

#include "core/kprintf.h"
#include "core/memory.h"
#include "core/syscalls.h"
#include "errno.h"
#include "fs/vfs/vfs.h"
#include "mm/heap.h"
#include "mm/uaccess.h"
#include "proc/sched.h"
#include "stdc.h"

#define COPY_BUFFER_SIZE 512

#define AT_FDCWD -100  // from Linux

// user expect the linux_dirent64.d_reclen be rounded up to 8
#define DIRENT_RECLEN_ROUND_UP(x) (((x) + 7) & ~7ULL)
#define DIRENT_NAME_OFFSET (offsetof(struct linux_dirent64, d_name))
#define DIRENT_BUFFER_SIZE DIRENT_RECLEN_ROUND_UP(DIRENT_NAME_OFFSET + VFS_NAME_SIZE)

static struct file **current_task_fds() {
    struct task *task = sched_get_current_task();
    if (!task) return NULL;
    return task->fds;
}

struct file *file_ref(struct file *f) {
    if (!f) return NULL;
    if (f->ref_count == 0) {
        // If the node has an open callback, give the driver a chance to initialize it
        if (f->node && f->node->ops && f->node->ops->open) {
            if (f->node->ops->open(f->node) != 0) return NULL;
        }
    }
    f->ref_count++;
    return f;
}

void file_unref(struct file *f) {
    if (!f) return;
    f->ref_count--;
    if (f->ref_count > 0) return;
    if (f->node && f->node->ops && f->node->ops->close) {
        // can fail, but we ignore it for now
        f->node->ops->close(f->node);
    }
    kfree(f);
}

struct file *file_open_node(struct vfs_node *node, int flags) {
    if (!node) return NULL;

    struct file *f = kmalloc(sizeof(struct file));
    if (!f) return NULL;

    f->node = node;
    f->offset = 0;
    f->flags = flags;
    f->ref_count = 0;

    if (!file_ref(f)) {
        kfree(f);
        return NULL;
    }

    return f;
}

int64_t sys_openat(int dirfd, const char *path, int flags, int mode) {
    (void)mode;                             // only means something once we honor O_CREAT
    if (dirfd != AT_FDCWD) return -ENOSYS;  // do not support dirfd relative open for now
    if (!path) return -ENOENT;

    // vfs_lookup will get the mount point and search each path segment with target->ops->finddir
    struct vfs_node *target_node = vfs_lookup(path);
    if (!target_node) return -ENOENT;

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

    fds[fd] = file_open_node(target_node, flags);
    if (!fds[fd]) return -ENOMEM;

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
    if (!f) return -EBADF;  // Already closed

    file_unref(f);
    fds[fd] = NULL;

    return 0;
}

int64_t sys_getdents64(int fd, void *dirp, uint64_t count) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) return -EBADF;
    if (!dirp) return -EFAULT;

    struct file **fds = current_task_fds();
    if (!fds) return -EBADF;

    struct file *f = fds[fd];
    if (!f || !f->node) return -EBADF;
    if (!(f->node->flags & VFS_DIRECTORY)) return -ENOTDIR;
    if (!f->node->ops || !f->node->ops->readdir) return -ENOSYS;

    // lot of trouble to ABI fit my VFS readdir to Linux getdents64
    // i guess this will be worth it when porting busybox

    uint64_t actual_count = 0;
    while (true) {
        struct vfs_dirent dirent;

        // Use f->offset as the directory index
        int res = f->node->ops->readdir(f->node, f->offset, &dirent);
        if (res != 1) {
            // no more entry, not an error
            if (res == 0) break;
            // nothing written yet, caller get an error
            if (actual_count == 0) return -EIO;
            // we have written something, let the caller know
            break;
        }

        size_t name_len = strlen(dirent.name) + 1;
        uint64_t reclen = DIRENT_RECLEN_ROUND_UP(DIRENT_NAME_OFFSET + name_len);
        // can we fit this entry in the caller buffer
        if (actual_count + reclen > count) {
            // nothing written yet, caller get an error
            if (actual_count == 0) return -EINVAL;
            // we have written something, let the caller know
            break;
        }

        struct linux_dirent64 hdr;
        hdr.d_ino = dirent.inode;
        hdr.d_off = (int64_t)(f->offset + 1);  // so lseek find the next entry
        hdr.d_reclen = (uint16_t)reclen;
        if (dirent.flags & VFS_FILE) {
            hdr.d_type = DT_REG;
        } else if (dirent.flags & VFS_DIRECTORY) {
            hdr.d_type = DT_DIR;
        } else if (dirent.flags & VFS_CHARDEVICE) {
            hdr.d_type = DT_CHR;
        } else {
            hdr.d_type = DT_UNKNOWN;
        }

        char chunk[DIRENT_BUFFER_SIZE];
        memset(chunk, 0, reclen);
        memcpy(chunk, &hdr, DIRENT_NAME_OFFSET);
        memcpy(chunk + DIRENT_NAME_OFFSET, dirent.name, name_len);

        res = copy_to_user((uint8_t *)dirp + actual_count, chunk, reclen);
        if (res != 0) {
            // nothing written yet, caller get an error
            if (actual_count == 0) return res;
            // we have written something, let the caller know
            break;
        }

        actual_count += reclen;
        f->offset++;
    }

    return actual_count;
}

int64_t sys_read(int fd, void *buf, uint64_t count) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) return -EBADF;
    if (!buf) return -EFAULT;

    struct file **fds = current_task_fds();
    if (!fds) return -EBADF;

    struct file *f = fds[fd];
    if (!f) return -EBADF;
    if (!f->node->ops || !f->node->ops->read) return -ENOSYS;  // Filesystem doesn't support reading

    char chunk[COPY_BUFFER_SIZE];
    int64_t total_count = 0;
    while ((uint64_t)total_count < count) {
        uint64_t chunk_len = (count - total_count) < COPY_BUFFER_SIZE ? (count - total_count) : COPY_BUFFER_SIZE;
        int64_t actual_chunk_len = f->node->ops->read(f->node, f->offset, chunk_len, chunk);
        if (actual_chunk_len > 0) {
            if (copy_to_user((uint8_t *)buf + total_count, chunk, actual_chunk_len) != 0) {
                // keep what already reached the caller
                return total_count > 0 ? total_count : -EFAULT;
            }
            total_count += actual_chunk_len;
            // one read is one line on a char stream, and there is no position to advance
            if (f->node->flags & VFS_CHARDEVICE) break;
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
    if ((f->flags & O_WRONLY) == 0 && (f->flags & O_RDWR) == 0) return -EACCES;  // File not opened for writing

    if (!f->node->ops || !f->node->ops->write) return -ENOSYS;  // Filesystem doesn't support writing

    char chunk[COPY_BUFFER_SIZE];
    int64_t total_count = 0;
    while ((uint64_t)total_count < count) {
        uint64_t chunk_len = (count - total_count) < COPY_BUFFER_SIZE ? (count - total_count) : COPY_BUFFER_SIZE;
        if (copy_from_user(chunk, (uint8_t *)buf + total_count, chunk_len) != 0) {
            // keep what already made it out
            return total_count > 0 ? total_count : -EFAULT;
        }
        int64_t actual_chunk_len = f->node->ops->write(f->node, f->offset, chunk_len, chunk);
        if (actual_chunk_len > 0) {
            total_count += actual_chunk_len;
            // a char stream has no position to advance
            if (f->node->flags & VFS_CHARDEVICE) break;
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

    // a char stream has no position to seek to
    if (f->node->flags & VFS_CHARDEVICE) return -ESPIPE;

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
