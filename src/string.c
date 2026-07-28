#include "string.h"

#include <stdint.h>

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

void *memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

static size_t utoa(uint64_t val, char *buf, size_t buf_size, int base, int uppercase) {
    char tmp[64];
    size_t i = 0;
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    if (val == 0) {
        tmp[i++] = '0';
    } else {
        while (val > 0) {
            tmp[i++] = digits[val % base];
            val /= base;
        }
    }

    size_t written = 0;
    while (i > 0 && written < buf_size) {
        buf[written++] = tmp[--i];
    }
    return written;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list args) {
    if (!buf || size == 0) return 0;

    size_t off = 0;

    #define APPEND_CHAR(c) do { \
        if (off < size - 1) buf[off++] = (c); \
    } while(0)

    while (*fmt) {
        if (*fmt != '%') {
            APPEND_CHAR(*fmt++);
            continue;
        }

        fmt++; // Skip '%'

        switch (*fmt) {
            case 'c': {
                char c = (char)va_arg(args, int);
                APPEND_CHAR(c);
                break;
            }
            case 's': {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                while (*s) {
                    APPEND_CHAR(*s++);
                }
                break;
            }
            case 'd':
            case 'i': {
                int32_t val = va_arg(args, int32_t);
                if (val < 0) {
                    APPEND_CHAR('-');
                    val = -val;
                }
                off += utoa((uint32_t)val, buf + off, size - off - 1, 10, 0);
                break;
            }
            case 'u': {
                uint32_t val = va_arg(args, uint32_t);
                off += utoa(val, buf + off, size - off - 1, 10, 0);
                break;
            }
            case 'x':
            case 'X':
            case 'p': {
                uint32_t val = va_arg(args, uint32_t);
                if (*fmt == 'p') {
                    APPEND_CHAR('0');
                    APPEND_CHAR('x');
                }
                off += utoa(val, buf + off, size - off - 1, 16, (*fmt == 'X'));
                break;
            }
            case '%': {
                APPEND_CHAR('%');
                break;
            }
            default:
                APPEND_CHAR('%');
                APPEND_CHAR(*fmt);
                break;
        }
        fmt++;
    }

    buf[off] = '\0';
    return (int)off;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return len;
}

