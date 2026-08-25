#ifndef PROS_USER_SYSCALL_H
#define PROS_USER_SYSCALL_H

// hand-hardcoded syscall ABI,
// on purpose: user/ never includes kernel/include/
// someday a libc will provide them

static inline long sys_read(long fd, void *buf, long len) {
#ifdef __x86_64__
    long ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(0), "D"(fd), "S"(buf), "d"(len)
        : "rcx", "r11", "memory");
    return ret;
#else
    register long x8 asm("x8") = 63;
    register long x0 asm("x0") = fd, x1 asm("x1") = (long)buf, x2 asm("x2") = len;
    asm volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2)
        : "memory");
    return x0;
#endif
}

static inline long sys_write(long fd, const void *buf, long len) {
#ifdef __x86_64__
    long ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(1), "D"(fd), "S"(buf), "d"(len)
        : "rcx", "r11", "memory");
    return ret;
#else
    register long x8 asm("x8") = 64;
    register long x0 asm("x0") = fd, x1 asm("x1") = (long)buf, x2 asm("x2") = len;
    asm volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2)
        : "memory");
    return x0;
#endif
}

// aarch64 has no real "open", 511 is PrOS-only, see unistd.h
static inline long sys_open(const char *path, long flags) {
#ifdef __x86_64__
    long ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(2), "D"(path), "S"(flags)
        : "rcx", "r11", "memory");
    return ret;
#else
    register long x8 asm("x8") = 511;
    register long x0 asm("x0") = (long)path, x1 asm("x1") = flags;
    asm volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1)
        : "memory");
    return x0;
#endif
}

// the real openat on both arches, kept for parity even though sys_open covers us today
static inline long sys_openat(long dirfd, const char *path, long flags, long mode) {
#ifdef __x86_64__
    long ret;
    // 4th syscall arg goes in r10, not rcx, syscall itself clobbers rcx
    register long r10 asm("r10") = mode;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(257), "D"(dirfd), "S"(path), "d"(flags), "r"(r10)
        : "rcx", "r11", "memory");
    return ret;
#else
    register long x8 asm("x8") = 56;
    register long x0 asm("x0") = dirfd, x1 asm("x1") = (long)path, x2 asm("x2") = flags, x3 asm("x3") = mode;
    asm volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2), "r"(x3)
        : "memory");
    return x0;
#endif
}

static inline long sys_close(long fd) {
#ifdef __x86_64__
    long ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(3), "D"(fd)
        : "rcx", "r11", "memory");
    return ret;
#else
    register long x8 asm("x8") = 57;
    register long x0 asm("x0") = fd;
    asm volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x8)
        : "memory");
    return x0;
#endif
}

static inline long sys_getpid(void) {
#ifdef __x86_64__
    long ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(39)
        : "rcx", "r11", "memory");
    return ret;
#else
    // no argument to pass in, x0 is pure output here unlike every other wrapper above
    register long x8 asm("x8") = 172;
    register long x0 asm("x0");
    asm volatile(
        "svc #0"
        : "=r"(x0)
        : "r"(x8)
        : "memory");
    return x0;
#endif
}

// returns bytes packed into dirp, 0 at end of dir, -errno on failure
// -EINVAL if dirp cannot even hold the first entry, so a short buffer is not mistaken for "done"
static inline long sys_getdents64(long fd, void *dirp, long count) {
#ifdef __x86_64__
    long ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(217), "D"(fd), "S"(dirp), "d"(count)
        : "rcx", "r11", "memory");
    return ret;
#else
    register long x8 asm("x8") = 61;
    register long x0 asm("x0") = fd, x1 asm("x1") = (long)dirp, x2 asm("x2") = count;
    asm volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2)
        : "memory");
    return x0;
#endif
}

// never actually returns once sched_exit_current() runs, callers should not rely on it coming back
static inline long sys_exit(long status) {
#ifdef __x86_64__
    long ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(60), "D"(status)
        : "rcx", "r11", "memory");
    return ret;
#else
    register long x8 asm("x8") = 93;
    register long x0 asm("x0") = status;
    asm volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x8)
        : "memory");
    return x0;
#endif
}

#endif  // PROS_USER_SYSCALL_H
