#include "io.h"
#include "syscall.h"

void _start(void) {
    PUTS_ERR("init start\n");

    int fd = sys_open("/root/hello.txt", O_RDONLY);
    if (fd >= 0) {
        char buf[512];
        long n = sys_read(fd, buf, sizeof(buf));
        if (n > 0) {
            // stdout
            sys_write(STDOUT, buf, n);
        }
        sys_close(fd);
    }

    PUTS_ERR("type here: ");
    char line[128];
    long n = sys_read(STDIN, line, sizeof(line));
    if (n > 0) {
        PUTS_ERR("you typed: ");
        sys_write(STDOUT, line, n);
    }

    PUTS_ERR("init exit\n");
    sys_exit(0);
}
