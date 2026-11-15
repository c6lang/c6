/* c6c 自带 <errno.h>：errno 是每进程一个整数，够用就行 */
#ifndef _C6C_ERRNO_H
#define _C6C_ERRNO_H

extern int errno;

#define EPERM 1
#define ENOENT 2
#define ESRCH 3
#define EINTR 4
#define EIO 5
#define ENXIO 6
#define E2BIG 7
#define ENOEXEC 8
#define EBADF 9
#define ECHILD 10
#define EAGAIN 11
#define EWOULDBLOCK EAGAIN
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define EBUSY 16
#define EEXIST 17
#define ENODEV 19
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENFILE 23
#define EMFILE 24
#define ENOSPC 28
#define ESPIPE 29
#define EROFS 30
#define EPIPE 32
#define ERANGE 34
#define ENOSYS 38

#endif
#define ETIMEDOUT 110

#ifndef EINPROGRESS
#define EINPROGRESS 115
#endif
#ifndef EALREADY
#define EALREADY 114
#endif
#ifndef ENOTSUP
#define ENOTSUP 95
#endif
#ifndef EOPNOTSUPP
#define EOPNOTSUPP 95
#endif
#ifndef EOVERFLOW
#define EOVERFLOW 75
#endif
#ifndef ENOTSOCK
#define ENOTSOCK 88
#endif
#ifndef EADDRINUSE
#define EADDRINUSE 98
#endif
#ifndef EADDRNOTAVAIL
#define EADDRNOTAVAIL 99
#endif
#ifndef ECONNRESET
#define ECONNRESET 104
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED 111
#endif
#ifndef EHOSTUNREACH
#define EHOSTUNREACH 113
#endif
#ifndef ENETUNREACH
#define ENETUNREACH 101
#endif
