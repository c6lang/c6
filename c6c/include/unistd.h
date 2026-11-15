/* c6c 自带 <unistd.h>：只声明运行时真有的那几个 */
#ifndef _C6C_UNISTD_H
#define _C6C_UNISTD_H

#include <stddef.h>
#include <sys/stat.h>   /* struct timespec（usleep 用） */

long read(int fd, void *buf, size_t n);
long write(int fd, const void *buf, size_t n);
long close(int fd);
long lseek(int fd, long off, int whence);
void _exit(int code);

typedef long ssize_t;
typedef int pid_t;

/* 进程/终端/临时文件：std/os 下那几个模块要用 */
int fork(void);
int waitpid(int pid, int *status, int options);
int isatty(int fd);
long ioctl(int fd, unsigned long req, void *arg);
int mkstemp(char *tmpl);
char *mkdtemp(char *tmpl);
int access(const char *path, int mode);
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
char *getcwd(char *buf, size_t size);
int chdir(const char *path);
int unlink(const char *path);
int setenv(const char *name, const char *val, int overwrite);
int unsetenv(const char *name);
int getpid(void);
int getuid(void);
int geteuid(void);
int setsid(void);
int dup(int fd);
int dup2(int oldfd, int newfd);
int nice(int inc);
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);

#endif

char *getcwd(char *buf, size_t size);
int unlink(const char *path);
int link(const char *oldpath, const char *newpath);
int symlink(const char *target, const char *linkpath);
long readlink(const char *path, char *buf, unsigned long bufsiz);
int rmdir(const char *path);
int chdir(const char *path);
int execvp(const char *file, char *const argv[]);
int execl(const char *path, const char *arg, ...);
int execv(const char *path, char *const argv[]);
extern char **environ;
int pipe(int fds[2]);
int pipe2(int fds[2], int flags);

/* sysconf：只要 c6c 自己的 std 用得到的那几个名字 */
long sysconf(int name);
#define _SC_PAGESIZE 30
#define _SC_PAGE_SIZE _SC_PAGESIZE
#define _SC_NPROCESSORS_ONLN 84
#define _SC_NPROCESSORS_CONF 83
#define _SC_CLK_TCK 2
#define _SC_OPEN_MAX 4
#define _SC_ARG_MAX 0
