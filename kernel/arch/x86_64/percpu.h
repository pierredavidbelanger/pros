#ifndef PROS_X86_64_PERCPU_H
#define PROS_X86_64_PERCPU_H

#include "stdc.h"

struct x86_64_percpu {
    uint64_t kernel_rsp; // offset 0, what syscall_entry swaps in
    uint64_t user_rsp;   // offset 8, stashed here while running kernel-side
};

extern struct x86_64_percpu x86_64_percpu0; // the only instance, for now

#endif //PROS_X86_64_PERCPU_H
