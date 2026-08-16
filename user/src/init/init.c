
#ifdef __x86_64__
static inline long sys_write(long fd, const void *buf, long len) {
    long ret;
    asm volatile ("syscall" : "=a"(ret) : "a"(1), "D"(fd), "S"(buf), "d"(len) : "rcx", "r11", "memory");
    return ret;
}
#else
static inline long sys_write(long fd, const void *buf, long len) {
    register long x8 asm("x8") = 64;
    register long x0 asm("x0") = fd, x1 asm("x1") = (long) buf, x2 asm("x2") = len;
    asm volatile ("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
}
#endif

void _start(void) {
    sys_write(1, "hello from /bin/init\n", 22);
    for (;;) {}
}
