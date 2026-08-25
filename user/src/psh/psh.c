#include "io.h"
#include "string.h"
#include "syscall.h"

void _start(void) {
    PUTS_OUT("PjErOS shell\n");
    // TODO: a prompt loop
    sys_exit(0);
}
