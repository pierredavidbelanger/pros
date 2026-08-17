#ifndef PROS_UACCESS_H
#define PROS_UACCESS_H

#include "stdc.h"

// Bound check [user_ptr, user_ptr + len) against VMM_ADDR_SPLIT, rejecting overflow before comparing.
// Returns 0 on success, -EFAULT if the range is invalid.
// Does not verify mappings, an unmapped access is allowed to fault for real.

int copy_from_user(void *kernel_dst, const void *user_src, uint64_t len);

int copy_to_user(void *user_dst, const void *kernel_src, uint64_t len);

#endif //PROS_UACCESS_H
