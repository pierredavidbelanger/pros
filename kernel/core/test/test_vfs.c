#include "core/test/test.h"

#include "core/kprintf.h"
#include "core/syscalls.h"
#include "fs/vfs/vfs.h"

void test_vfs(void) {
    int fd = sys_open("/root", 0);
    if (fd >= 0) {
        kprintf("VFS", "/root fd=%d\n", fd);
        kprintf("VFS", "----------- ls /root ----------\n");
        struct vfs_dirent entry;
        while (sys_readdir(fd, &entry) == 1) {
            if (entry.flags == VFS_DIRECTORY) {
                kprintf("VFS", "%s/\n", entry.name);
            } else {
                kprintf("VFS", "%s\n", entry.name);
            }
        }
        kprintf("VFS", "-------------------------------\n");
        sys_close(fd);
    } else {
        kprintf("VFS", "cannot open /\n");
    }
    fd = sys_open("/root/hello.txt", O_RDONLY);
    if (fd >= 0) {
        kprintf("VFS", "/root/hello.txt fd=%d\n", fd);
        char buffer[2000];
        int64_t bytes_read = sys_read(fd, buffer, 2000);
        if (bytes_read > 0) {
            // Null-terminate it just to be safe before printing
            buffer[bytes_read] = '\0';
            kprintf("VFS", "------ cat root/hello.txt -----\n");
            kprintf("VFS", "%s\n", buffer);
            kprintf("VFS", "-------------------------------\n");
        }
        sys_close(fd);
    } else {
        kprintf("VFS", "cannot open /root/hello.txt\n");
    }
}
