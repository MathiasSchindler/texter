#ifndef RT_RT_H
#define RT_RT_H

#include "rt/types.h"

#define RT_O_RDONLY 0
#define RT_O_WRONLY 1
#define RT_O_RDWR 2
#define RT_O_CREAT 64
#define RT_O_TRUNC 512
#define RT_AT_FDCWD (-100)

void rt_exit(int code) __attribute__((noreturn));
long rt_openat(int dirfd, const char* path, int flags, int mode);
long rt_read(int fd, void* buf, usize count);
long rt_write(int fd, const void* buf, usize count);
long rt_close(int fd);

usize rt_strlen(const char* s);
long rt_write_all(int fd, const void* buf, usize count);

/* Writes unsigned decimal into out buffer and returns bytes written. */
usize rt_u64_to_dec(u64 value, char* out, usize out_cap);

#endif
