#ifndef PROS_MEMORY_H
#define PROS_MEMORY_H

#include "stdc.h"

void *memcpy(void *restrict dest, const void *restrict src, size_t n);
void *memset(void *s, int c, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
void strntrim(char *s, char t, size_t n);
char *strstr(const char *haystack, const char *needle);

#endif //PROS_MEMORY_H
