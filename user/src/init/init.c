#include "syscall.h"

// hardcoded constants,
// init must not include kernel related stuff

void _start(void) {
    int fd = sys_open("/root/hello.txt", /* O_RDONLY */ 0x0000);
    if (fd >= 0) {
        char buf[512];
        long n = sys_read(fd, buf, sizeof(buf));
        if (n > 0) {
            // stdout
            sys_write(1, buf, n);
        }
        sys_close(fd);
    }

    sys_exit(0);
}
