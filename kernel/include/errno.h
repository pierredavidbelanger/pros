#ifndef PROS_ERRNO_H
#define PROS_ERRNO_H

// Linux's real numbers, not our own numbering, a libc expects these exact values
#define ENOENT 2    // no such file or directory
#define EIO 5       // input/output error
#define EBADF 9     // bad file descriptor
#define EAGAIN 11   // resource temporarily unavailable, try again
#define ENOMEM 12   // out of memory
#define EACCES 13   // permission denied
#define EFAULT 14   // bad address, points outside our accessible space
#define ENOTDIR 20  // not a directory
#define EINVAL 22   // invalid argument
#define ESPIPE 29   // illegal seek, this file is a stream with no position
#define ENOSYS 38   // function not implemented

#endif  // PROS_ERRNO_H
