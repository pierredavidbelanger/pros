#ifndef PROS_STRING_H
#define PROS_STRING_H

#include <stddef.h>
#include <stdarg.h>

size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memchr(const void *s, int c, size_t n);

/**
 * @brief Formats and stores a series of characters and values in a buffer using a va_list.
 *
 * Supported format specifiers:
 *   - %c : Single character
 *   - %s : Null-terminated string
 *   - %d, %i : Signed 32-bit decimal integer
 *   - %u : Unsigned 32-bit decimal integer
 *   - %x, %X : Unsigned 32-bit hexadecimal integer (lowercase / uppercase)
 *   - %p : Pointer address formatted with '0x' prefix
 *   - %% : Literal '%' character
 *
 * @param buf Pointer to the destination output buffer.
 * @param size Maximum number of bytes to write (including the null-terminator).
 * @param fmt Format control string.
 * @param args Variadic argument list handle (`va_list`).
 * @return Number of characters written to `buf` (excluding the null-terminator).
 */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
int snprintf(char *buf, size_t size, const char *fmt, ...);

#endif // PROS_STRING_H
