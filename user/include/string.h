#ifndef PROS_USER_STRING_H
#define PROS_USER_STRING_H

// hand-rolled string helpers,
// on purpose: user/ never includes kernel/include/
// someday a libc will provide them

#define NULL ((void*)0)

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


// check if a character matches any delimiter
static inline int is_delim(char c, const char *delim) {
    while (*delim) {
        if (c == *delim++) return 1;
    }
    return 0;
}

static inline char *strtokr(char *str, const char *delim, char **saveptr) {
    // If str is NULL, pick up where the saveptr context left off
    char *next = (str != NULL) ? str : *saveptr;

    if (next == NULL || *next == '\0') {
        *saveptr = NULL;
        return NULL;
    }

    // Skip leading delimiters
    while (*next && is_delim(*next, delim)) {
        next++;
    }

    if (*next == '\0') {
        *saveptr = next;
        return NULL;
    }

    // Mark the start of the token
    char *token_start = next;

    // Scan for the end of the token
    while (*next) {
        if (is_delim(*next, delim)) {
            *next = '\0';         // Null-terminate the token
            *saveptr = next + 1;  // Save the next position into the caller's context
            return token_start;
        }
        next++;
    }

    // End of string reached
    *saveptr = next;
    return token_start;
}

#endif  // PROS_USER_STRING_H
