#include "core/memory.h"

// GCC and Clang reserve the right to generate calls to the following
// 4 functions even if they are not directly called.
// They must be implemented as the C specification mandates.
// DO NOT remove or rename these functions, or stuff will eventually break!

void *memcpy(void *restrict dest, const void *restrict src, size_t n) {
    uint8_t *restrict pdest = dest;
    const uint8_t *restrict psrc = src;

    for (size_t i = 0; i < n; i++) {
        pdest[i] = psrc[i];
    }

    return dest;
}

void *memset(void *s, int c, size_t n) {
    uint8_t *p = s;

    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }

    return s;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *pdest = dest;
    const uint8_t *psrc = src;

    if ((uintptr_t)src > (uintptr_t)dest) {
        for (size_t i = 0; i < n; i++) {
            pdest[i] = psrc[i];
        }
    } else if ((uintptr_t)src < (uintptr_t)dest) {
        for (size_t i = n; i > 0; i--) {
            pdest[i-1] = psrc[i-1];
        }
    }

    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = s1;
    const uint8_t *p2 = s2;

    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }

    return 0;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) {
        len++;
    }
    return len;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;

    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        if (!*n) return (char *)haystack;
    }

    return NULL;
}

void strntrim(char *s, const char t, size_t n) {
    if (s == NULL || n == 0) return;

    size_t len = 0;
    while (len < n && s[len] != '\0') {
        len++;
    }

    if (len == 0) return;

    size_t start = 0;
    while (start < len && s[start] == t) {
        start++;
    }

    size_t end = len;
    while (end > start && s[end - 1] == t) {
        end--;
    }

    size_t new_len = end - start;
    for (size_t i = 0; i < new_len; i++) {
        s[i] = s[start + i];
    }

    if (new_len < n) {
        s[new_len] = '\0';
    }
}

// check if a character matches any delimiter
static int is_delim(char c, const char *delim) {
    while (*delim) {
        if (c == *delim++) return 1;
    }
    return 0;
}

char *strtokr(char *str, const char *delim, char **saveptr) {
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
            *next = '\0';          // Null-terminate the token
            *saveptr = next + 1;   // Save the next position into the caller's context
            return token_start;
        }
        next++;
    }

    // End of string reached
    *saveptr = next;
    return token_start;
}
