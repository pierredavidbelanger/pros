#include "mm/uaccess.h"

#include "core/memory.h"
#include "mm/vmm.h"

#include "errno.h"

int copy_from_user(void *kernel_dst, const void *user_src, uint64_t len) {
    uintptr_t start = (uintptr_t) user_src;
    uintptr_t end = start + len;
    if (end < start) return -EFAULT; // len wrapped the addition
    if (end > VMM_ADDR_SPLIT) return -EFAULT; // range spills into kernel space
    memcpy(kernel_dst, user_src, len);
    return 0;
}

int copy_to_user(void *user_dst, const void *kernel_src, uint64_t len) {
    uintptr_t start = (uintptr_t) user_dst;
    uintptr_t end = start + len;
    if (end < start) return -EFAULT; // len wrapped the addition
    if (end > VMM_ADDR_SPLIT) return -EFAULT; // range spills into kernel space
    memcpy(user_dst, kernel_src, len);
    return 0;
}
