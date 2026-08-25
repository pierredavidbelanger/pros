#ifndef PROS_USER_STRING_H
#define PROS_USER_STRING_H

// hand-rolled string helpers,
// on purpose: user/ never includes kernel/include/
// someday a libc will provide them

// no stddef.h under -nostdinc, but the compiler still tells us what size_t is
typedef __SIZE_TYPE__ size_t;

static inline size_t strnlen(const char *s, size_t n) {
    size_t len = 0;
    while (n && s[len]) {
        len++;
        n--;
    }
    return len;
}

// bounded on purpose, compare with sizeof the literal so "exit" doesnt match "exitfoo"
static inline int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

#endif  // PROS_USER_STRING_H
