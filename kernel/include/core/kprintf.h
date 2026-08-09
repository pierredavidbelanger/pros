#ifndef PROS_KPRINTF_H
#define PROS_KPRINTF_H

#include <printf.h>

#include "stdc.h"

#define KPRINTF_TAG_WIDTH 5

void kprintf(const char *tag, const char *format, ...);

void kpanic(const char *msg);

#endif //PROS_KPRINTF_H
