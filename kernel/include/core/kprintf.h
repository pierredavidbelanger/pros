#ifndef PROS_KPRINTF_H
#define PROS_KPRINTF_H

#include <printf.h>

#include "stdc.h"

#define KPRINTF_TAG_WIDTH 5

#define TERM_COLOR_RESET        "\033[0m"
#define TERM_COLOR_RED          "\033[31m"
#define TERM_COLOR_GREEN        "\033[32m"
#define TERM_COLOR_DARK_GRAY    "\033[90m"

void kprintf(const char *tag, const char *format, ...);

void kpanic(const char *msg);

#endif //PROS_KPRINTF_H
