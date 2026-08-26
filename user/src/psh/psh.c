#include "io.h"
#include "string.h"
#include "syscall.h"

#define PSH_BUF_MAX 512
#define PSH_ARGV_MAX ((PSH_BUF_MAX / 2) + 1)

void _start(void) {
    PUTS_OUT("PjErOS shell\n");

    char buf[PSH_BUF_MAX];

    while (1) {
        PUTS_OUT("# ");

        long n = sys_read(STDIN, buf, PSH_BUF_MAX);
        if (n <= 0) continue;
        buf[n - 1] = '\0'; // strip the \n

        int argc = 0;
        char *argv[PSH_ARGV_MAX];

        char *saveptr = NULL;
        char *token = strtokr(buf, " ", &saveptr);
        while (token != NULL) {
            argv[argc] = token;
            argc++;
            token = strtokr(NULL, " ", &saveptr);
        }

        if (argc == 0) continue;

        if (BUF_EQ_S(argv[0], "help")) {
            PUTS_ERR("Commands:\n");
            PUTS_ERR("\thelp: print this help\n");
            PUTS_ERR("\techo arg: print arg\n");
            PUTS_ERR("\tcat arg: open arg and print its content\n");
            PUTS_ERR("\texit: exit psh\n");
            continue;
        }

        if (BUF_EQ_S(argv[0], "echo")) {
            for (int i = 1; i < argc; i++) {
                sys_write(STDOUT, argv[i], (long) strnlen(argv[i], PSH_BUF_MAX));
                PUTS_OUT(" ");
            }
            PUTS_OUT("\n");
            continue;
        }

        if (BUF_EQ_S(argv[0], "cat")) {
            for (int i = 1; i < argc; i++) {
                int fd = sys_open(argv[i], O_RDONLY);
                if (fd < 0) {
                    PUTS_ERR("Cannot open '");
                    sys_write(STDERR, argv[i], (long) strnlen(argv[i], PSH_BUF_MAX));
                    PUTS_ERR("'\n");
                    continue;
                }
                char buf[512];
                long len = sys_read(fd, buf, sizeof(buf));
                if (len >= 0) {
                    while (len > 0) {
                        sys_write(STDOUT, buf, len);
                        len = sys_read(fd, buf, sizeof(buf));
                    }
                } else {
                    PUTS_ERR("Cannot read '");
                    sys_write(STDERR, argv[i], (long) strnlen(argv[i], PSH_BUF_MAX));
                    PUTS_ERR("'\n");
                }
                sys_close(fd);
            }
            continue;
        }

        if (BUF_EQ_S(argv[0], "exit")) {
            break;
        }

        PUTS_ERR("Command '");
        sys_write(STDERR, argv[0], (long) strnlen(argv[0], PSH_BUF_MAX));
        PUTS_ERR("' not found\n");
    }

    sys_exit(0);
}
