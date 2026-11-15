/* c6c 自带 <fcntl.h> */
#ifndef _C6C_FCNTL_H
#define _C6C_FCNTL_H

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0100
#define O_TRUNC 01000
#define O_APPEND 02000

/* 记录锁（POSIX `fcntl` 的 F_SETLK / F_SETLKW）：std/os/lock 要用。
 * 布局照 x86-64 Linux 的 `struct flock`。 */
struct flock {
    short l_type;
    short l_whence;
    long l_start;
    long l_len;
    int l_pid;
};

#define F_RDLCK 0
#define F_WRLCK 1
#define F_UNLCK 2

long open(const char *path, long flags, long mode);

/* pty：std/os/tty 要用 */
int posix_openpt(int flags);
int grantpt(int fd);
int unlockpt(int fd);
char *ptsname(int fd);

#endif
int fcntl(int fd, int cmd, ...);
int openat(int dirfd, const char *path, int flags, ...);

#ifndef F_GETFL
#define F_DUPFD  0
#define F_GETFD  1
#define F_SETFD  2
#define F_GETFL  3
#define F_SETFL  4
#define F_GETLK  5
#define F_SETLK  6
#define F_SETLKW 7
#define F_SETOWN 8
#define F_GETOWN 9
#define FD_CLOEXEC 1
#endif
#ifndef O_NONBLOCK
#define O_RDONLY    00
#define O_WRONLY    01
#define O_RDWR      02
#define O_ACCMODE   03
#define O_CREAT     0100
#define O_EXCL      0200
#define O_NOCTTY    0400
#define O_TRUNC     01000
#define O_APPEND    02000
#define O_NONBLOCK  04000
#define O_NDELAY    O_NONBLOCK
#define O_SYNC      04010000
#define O_DSYNC     010000
#define O_ASYNC     020000
#define O_DIRECTORY 0200000
#define O_NOFOLLOW  0400000
#define O_CLOEXEC   02000000
#endif
