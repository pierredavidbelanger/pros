#ifndef PROS_SYSCALL_H
#define PROS_SYSCALL_H

#include "stdc.h"

int64_t syscall_dispatch(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);

#endif //PROS_SYSCALL_H
