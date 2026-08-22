#ifndef PROS_ERRNO_H
#define PROS_ERRNO_H

// Linux's real numbers, not our own numbering, a libc expects these exact values
#define ENOENT 2   // no such file or directory
#define EIO 5      // no such file or directory
#define EBADF 9    // bad file descriptor
#define EAGAIN 11  // resource temporarily unavailable, try again
#define ENOMEM 12  // out of memory
#define EACCES 13  // permission denied
#define EFAULT 14  // bad address, points outside our accessible space
#define EINVAL 22  // invalid argument
#define ENOSYS 38  // function not implemented

#endif  // PROS_ERRNO_H
