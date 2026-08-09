#ifndef PROS_KPRINTF_H
#define PROS_KPRINTF_H

#include <printf.h>

#define kprintf printf

void kpanic(const char *msg);

#endif //PROS_KPRINTF_H
