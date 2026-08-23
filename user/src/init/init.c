#include "syscall.h"

// hardcoded constants,
// init must not include kernel related stuff

#define STDIN 0
#define STDOUT 1
#define STDERR 2

#define O_RDONLY 0x0000

#define PUTS(s)
#define GETS(s) sys_read(STDIN, (s), sizeof(s))

void _start(void) {
    sys_write(STDERR, "init start\n", 11);

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

    sys_write(STDERR, "type here: ", 11);
    char line[128];
    long n = sys_read(STDIN, line, sizeof(line));
    if (n > 0) {
        sys_write(STDERR, "you typed: ", 11);
        sys_write(STDOUT, line, n);
    }

    sys_write(STDERR, "init exit\n", 10);
    sys_exit(0);
}
